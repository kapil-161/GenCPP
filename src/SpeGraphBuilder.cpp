#include "SpeGraphBuilder.h"
#include <QRegularExpression>
#include <QMap>
#include <algorithm>
#include <cmath>

namespace {

// DSSAT CURV() function types (Utilities/UTILS.for) used after 4 values in SPE files
const QStringList FUNC_TYPES = {"LIN","QDR","NON","INL","SHO","LON","SIN",
                                "REV","DHD","DRD","CDD","VOP"};

// Port of DSSAT CURV(CTYPE,XB,X1,X2,XM,X), Utilities/UTILS.for. For INL, SHO,
// LON, REV, DHD, DRD and CDD the 4th value (and for DRD/CDD the 3rd) is a
// response level, not an X position.
double curv(const QString &t, double xb, double x1, double x2, double xm, double x)
{
    double c = 1.0;
    if (t == "NON") return 1.0;
    if (t == "LIN") {
        c = 0.0;
        if (x > xb && x < x1) c = (x - xb) / (x1 - xb);
        if (x >= x1 && x <= x2) c = 1.0;
        if (x > x2 && x < xm) c = 1.0 - (x - x2) / (xm - x2);
        return std::clamp(c, 0.0, 1.0);
    }
    if (t == "QDR") {
        c = 0.0;
        if (x > xb && x < x1) c = 1.0 - std::pow((x1 - x) / (x1 - xb), 2);
        if (x >= x1 && x <= x2) c = 1.0;
        if (x > x2 && x < xm) c = 1.0 - std::pow((x - x2) / (xm - x2), 2);
        return std::clamp(c, 0.0, 1.0);
    }
    if (t == "INL" || t == "SHO") {
        if (x > x1 && x < x2) c = 1.0 - (1.0 - xm) * ((x - x1) / (x2 - x1));
        if (x >= x2) c = xm;
        return std::min(std::max(c, xm), 1.0);
    }
    if (t == "LON") {
        if (x < x2) c = xm;
        else if (x < x1) c = 1.0 - (1.0 - xm) * ((x1 - x) / (x1 - x2));
        else c = 1.0;
        return std::min(std::max(c, xm), 1.0);
    }
    if (t == "SIN") {
        const double k = 2.0 * 22.0 / 7.0;
        c = 0.0;
        if (x > xb && x < x1) c = 0.5 * (1.0 + std::cos(k * (x - x1) / (2.0 * (x1 - xb))));
        if (x >= x1 && x <= x2) c = 1.0;
        if (x > x2 && x < xm) c = 0.5 * (1.0 + std::cos(k * (x2 - x) / (2.0 * (xm - x2))));
        return std::clamp(c, 0.0, 1.0);
    }
    if (t == "REV") {
        c = 1.0;
        if (x > xb && x < x1) c = 1.0 - ((x - xb) / (x1 - xb));
        if (x > x1 && x < x2) c = 0.0 - ((x - x1) / (x2 - x1));
        if (x > x2) c = -1.0;
        return std::clamp(c, -1.0, 1.0) * xm;
    }
    if (t == "DHD") {
        c = 0.0;
        if (x > xb && x < x1) c = (x - xb) / (x1 - xb);
        if (x >= x1) c = 1.0;
        return std::clamp(c, 0.0, 1.0) * xm;
    }
    if (t == "DRD") {
        c = x2;
        if (x > xb && x < x1) c = x2 + (xm - x2) * (x - xb) / (x1 - xb);
        if (x >= x1) c = xm;
        return std::min(std::max(c, x2), xm);
    }
    if (t == "CDD") {
        c = x2;
        if (x > xb && x < x1) c = xm - ((xm - x2) * std::pow((x1 - x) / (x1 - xb), 2));
        if (x >= x1) c = xm;
        return std::min(std::max(c, x2), xm);
    }
    if (t == "VOP") {
        c = 0.0;
        if (x > xb && x < xm)
            c = (std::pow(x - xb, x2) * (xm - x)) / (std::pow(x1 - xb, x2) * (xm - x1));
        return std::max(c, 0.0);
    }
    return c;
}

// CURV sampled over the X span its type uses, padded so flat ends show.
SpeGraphSeries curvSeries(const QString &label, const QString &type,
                          double xb, double x1, double x2, double xm)
{
    double lo = xb, hi = xm;                                 // LIN, QDR, SIN, VOP
    if (type == "INL" || type == "SHO")      { lo = x1; hi = x2; }
    else if (type == "LON")                  { lo = x2; hi = x1; }
    else if (type == "REV")                  { lo = xb; hi = x2; }
    else if (type == "DHD" || type == "DRD" || type == "CDD") { lo = xb; hi = x1; }
    else if (type == "NON")                  { lo = std::min({xb, x1, x2, xm}); hi = std::max({xb, x1, x2, xm}); }
    if (lo > hi) std::swap(lo, hi);
    double pad = (hi - lo) * 0.15;
    if (pad <= 0) pad = 1.0;
    lo -= pad; hi += pad;

    SpeGraphSeries s;
    s.label = label;
    const int n = 160;
    for (int i = 0; i <= n; ++i) {
        double x = lo + (hi - lo) * i / n;
        double y = curv(type, xb, x1, x2, xm, x);
        if (std::isfinite(y)) s.points.append({x, y});
    }
    return s;
}

QStringList tokens(const QString &s)
{
    static const QRegularExpression ws("\\s+");
    return s.split(ws, Qt::SkipEmptyParts);
}

// Parse a SPE data line. If the first token is non-numeric and not a func
// type, it is treated as a CERES-style leading keyword (returned). The
// remaining tokens are split into nums, funcType, and trailing comment.
QString parseLine(const QString &text, QVector<double> &nums,
                  QString &funcType, QString &comment)
{
    nums.clear(); funcType.clear(); comment.clear();
    QString keyword;
    QStringList parts = tokens(text.trimmed());
    if (parts.isEmpty()) return keyword;

    // Leading words before the numbers; the last is the keyword
    // ("PRFTC 6.2 ...", SUBSTOR "PT OPT CO2X 0 220 ...").
    int startIdx = 0;
    while (startIdx < parts.size() && startIdx < 3) {
        bool ok;
        parts[startIdx].toDouble(&ok);
        if (ok || FUNC_TYPES.contains(parts[startIdx].toUpper())) break;
        keyword = parts[startIdx++];
    }
    if (startIdx == parts.size() || startIdx == 3) {
        bool ok = false;
        if (startIdx < parts.size()) parts[startIdx].toDouble(&ok);
        if (!ok) { keyword = parts[0]; startIdx = 1; }    // words only: keep the old reading
    }

    bool pastNums = false;
    for (int i = startIdx; i < parts.size(); ++i) {
        const QString &t = parts[i];
        if (!pastNums) {
            bool ok;
            double v = t.toDouble(&ok);
            if (ok) { nums.append(v); continue; }
            pastNums = true;
            if (FUNC_TYPES.contains(t.toUpper())) {
                funcType = t.toUpper();
                continue;
            }
        }
        comment += (comment.isEmpty() ? "" : " ") + t;
    }
    return keyword;
}

// Extract a clean parameter name from a trailing comment string.
QString paramFromComment(const QString &comment)
{
    if (comment.isEmpty()) return {};
    QString name = tokens(comment).first();
    int p = name.indexOf('(');
    if (p > 0) name = name.left(p);
    name.remove(QRegularExpression("[,;:\\-]+$"));
    return name;
}

// First comment token that looks like an X... or Y... array name.
QString axisName(const QString &comment, QChar axis)
{
    for (const QString &tok : tokens(comment))
        if (tok.size() >= 2 && tok[0].toUpper() == axis && tok[1].isLetter())
            return tok;
    return {};
}

// Index of the next/previous data line, skipping blank and comment-only lines;
// -1 at a section boundary (*) or the file edge.
int nextDataLine(const QStringList &lines, int i, int dir)
{
    for (i += dir; i >= 0 && i < lines.size(); i += dir) {
        QString t = lines[i].trimmed();
        if (t.startsWith('*')) return -1;
        if (!t.isEmpty() && !t.startsWith('!')) return i;
    }
    return -1;
}

// Linear [Tb,To1,To2,Tm] trapezoid (cardinal temperatures without a curve type).
SpeGraphSeries make4ValSeries(const QString &label, double tb, double to1,
                              double to2, double tm, const QString &funcType)
{
    if (!funcType.isEmpty() && funcType != "LIN")
        return curvSeries(label, funcType, tb, to1, to2, tm);
    SpeGraphSeries s;
    s.label = label;
    s.points = { {tb,0.0}, {to1,1.0}, {to2,1.0}, {tm,0.0} };
    return s;
}

bool increasing(const QVector<double> &v, int n)
{
    for (int i = 1; i < n && i < v.size(); ++i)
        if (v[i] < v[i - 1]) return false;
    return v.size() >= n;
}

// Description after the parameter names, e.g. "FNFXA(4),TYPFXA-AGE EFF ON N FIX"
// -> "AGE EFF ON N FIX", "LRMOB(4), TYPLMOB LAI EFF ON MOBILIZATION" -> "LAI EFF ...".
QString curvDescription(const QString &comment)
{
    QStringList tk = tokens(comment);
    if (tk.isEmpty()) return {};
    QString lead;
    QString first = tk.takeFirst();
    if (first.contains('-')) lead = first.section('-', 1);
    if (!tk.isEmpty() && tk.first().startsWith("TYP", Qt::CaseInsensitive)) {
        QString t0 = tk.takeFirst();
        if (t0.contains('-')) lead = t0.section('-', 1);
    }
    QString d = (lead + " " + tk.join(' ')).trimmed();
    return d.left(70);
}

// Section bounds (lines between '*' headers) around idx.
void sectionBounds(const QStringList &lines, int idx, int &from, int &to)
{
    from = 0; to = lines.size() - 1;
    for (int i = idx - 1; i >= 0; --i)
        if (lines[i].trimmed().startsWith('*')) { from = i + 1; break; }
    for (int i = idx + 1; i < lines.size(); ++i)
        if (lines[i].trimmed().startsWith('*')) { to = i - 1; break; }
}

// Keyword of a keyword-first line without an "(n)" size suffix: "XLMAXT(6)" -> "XLMAXT".
QString arrayName(QString kw)
{
    static const QRegularExpression sz("\\(\\d+\\)$");
    return kw.remove(sz);
}

SpeGraphData xySeries(const QString &xName, const QVector<double> &xs,
                      const QVector<QPair<QString, QVector<double>>> &ys)
{
    SpeGraphData d;
    d.xAxisLabel = xName;
    d.yAxisLabel = ys.size() == 1 ? ys.first().first : QString("Value");
    QStringList yNames;
    for (const auto &y : ys) {
        SpeGraphSeries s;
        s.label = y.first;
        for (int i = 0; i < xs.size() && i < y.second.size(); ++i)
            s.points.append({xs[i], y.second[i]});
        d.series.append(s);
        yNames << y.first;
    }
    d.title = yNames.join(", ") + " vs " + xName;
    return d;
}

// Keyword arrays (APSIM / CASUPRO / CERES): "XLMAXT(6) ..." with "YLMAXT(6) ..."
// anywhere in the section, or a run of adjacent keyword lines of equal length
// whose first line increases ("DTTPT 20 80 120" / "DTTF 0 0 0").
bool keywordArrays(const QStringList &lines, int idx, const QString &keyword,
                   const QVector<double> &nums, SpeGraphData &out)
{
    const QString name = arrayName(keyword);
    const QString up = name.toUpper();

    // X.../Y... prefix pair in the same section
    if (up.size() >= 3 && (up[0] == 'X' || up[0] == 'Y')) {
        const QString other = QString(up[0] == 'X' ? 'Y' : 'X') + up.mid(1);
        int from, to;
        sectionBounds(lines, idx, from, to);
        for (int i = from; i <= to; ++i) {
            if (i == idx) continue;
            QVector<double> o; QString ft, com;
            const QString kw = parseLine(lines[i].trimmed(), o, ft, com);
            if (kw.isEmpty() || arrayName(kw).toUpper() != other || o.size() != nums.size()) continue;
            const bool isX = up[0] == 'X';
            out = xySeries(isX ? name : arrayName(kw), isX ? nums : o,
                           {{isX ? arrayName(kw) : name, isX ? o : nums}});
            return true;
        }
    }

    // Run of adjacent keyword lines with the same number of values
    auto kwLine = [&](int i, QString &kw, QVector<double> &v) {
        QString ft, com;
        kw = parseLine(lines[i].trimmed(), v, ft, com);
        return !kw.isEmpty() && v.size() == nums.size();
    };
    int first = idx, last = idx;
    for (int j = nextDataLine(lines, first, -1); j >= 0; j = nextDataLine(lines, first, -1)) {
        QString kw; QVector<double> v;
        if (!kwLine(j, kw, v)) break;
        first = j;
    }
    for (int j = nextDataLine(lines, last, +1); j >= 0; j = nextDataLine(lines, last, +1)) {
        QString kw; QVector<double> v;
        if (!kwLine(j, kw, v)) break;
        last = j;
    }
    if (first == last) return false;

    QString xKw; QVector<double> xs;
    kwLine(first, xKw, xs);
    if (!increasing(xs, xs.size())) return false;     // X arrays increase
    QVector<QPair<QString, QVector<double>>> ys;
    for (int i = nextDataLine(lines, first, +1); i >= 0 && i <= last; i = nextDataLine(lines, i, +1)) {
        QString kw; QVector<double> v;
        kwLine(i, kw, v);
        ys.append({arrayName(kw), v});
    }
    out = xySeries(arrayName(xKw), xs, ys);
    return true;
}

// Array names in a dotted comment directly above a data line:
// "! .......XSTAGE.........  .......XSENMX........." -> {XSTAGE, XSENMX}
QStringList dottedNames(const QStringList &lines, int dataIdx)
{
    if (dataIdx <= 0) return {};
    const QString c = lines[dataIdx - 1].trimmed();
    if (!c.startsWith('!')) return {};
    static const QRegularExpression re("\\.{3,}([A-Za-z0-9#_]+)\\.{3,}");
    QStringList names;
    for (auto it = re.globalMatch(c); it.hasNext();) names << it.next().captured(1);
    return names;
}

// CROPGRO lines holding several equal-length arrays named by a dotted comment;
// the X arrays (XSTAGE, XSENMX) are on one line, their Y arrays on the other.
bool dottedArrays(const QStringList &lines, int idx, const QVector<double> &nums,
                  SpeGraphData &out)
{
    const QStringList names = dottedNames(lines, idx);
    if (names.isEmpty() || nums.size() % names.size() != 0) return false;
    for (int dir : {+1, -1}) {
        const int j = nextDataLine(lines, idx, dir);
        if (j < 0) continue;
        const QStringList other = dottedNames(lines, j);
        QVector<double> o; QString ft, com;
        parseLine(lines[j].trimmed(), o, ft, com);
        if (other.size() != names.size() || o.size() != nums.size()) continue;
        const bool hereIsX = names.first().startsWith('X', Qt::CaseInsensitive);
        if (hereIsX == other.first().startsWith('X', Qt::CaseInsensitive)) continue;
        const QStringList &xN = hereIsX ? names : other, &yN = hereIsX ? other : names;
        const QVector<double> &xv = hereIsX ? nums : o, &yv = hereIsX ? o : nums;
        const int k = nums.size() / names.size();
        SpeGraphData d;
        QStringList parts;
        for (int a = 0; a < names.size(); ++a) {
            SpeGraphSeries s;
            s.label = yN[a] + " (" + xN[a] + ")";
            for (int i = 0; i < k; ++i) s.points.append({xv[a * k + i], yv[a * k + i]});
            d.series.append(s);
            parts << yN[a] + " vs " + xN[a];
        }
        d.title = parts.join(",  ");
        d.xAxisLabel = xN.join(" / ");
        d.yAxisLabel = names.size() == 1 ? yN.first() : QString("Value");
        out = d;
        return true;
    }
    return false;
}

// Rows under a "!  TB   TO1   TO2    TM" comment: one trapezoid per row,
// e.g. the CROPGRO vegetative / early / late reproductive development rates.
bool cardinalTable(const QStringList &lines, int idx, SpeGraphData &out)
{
    static const QRegularExpression hdrRe("^!.*\\bTB\\b.*\\bTO1\\b.*\\bTO2\\b.*\\bTM\\b");
    int hdr = -1;
    for (int i = idx - 1; i >= 0; --i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith('*')) return false;
        if (t.startsWith('!')) { if (hdrRe.match(t).hasMatch()) hdr = i; break; }
    }
    if (hdr < 0) return false;

    SpeGraphData d;
    d.title = "Cardinal temperatures (TB, TO1, TO2, TM)";
    d.xAxisLabel = "Temperature (°C)";
    d.yAxisLabel = "Relative rate";
    for (int i = hdr + 1; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith('!') || t.startsWith('*') || t.startsWith('@')) break;
        QVector<double> v; QString ft, com;
        parseLine(t, v, ft, com);
        if (!increasing(v, 4)) continue;
        QString label = com.isEmpty() ? QString("Row %1").arg(d.series.size() + 1) : com;
        d.series.append(make4ValSeries(label.left(32), v[0], v[1], v[2], v[3], ""));
    }
    if (d.series.isEmpty()) return false;
    out = d;
    return true;
}

// *PHOSPHORUS CONTENT style rows: 3 values per organ at named growth stages,
// "0.0030  0.0025  0.0015  Optimum Leaf Conc  (emergence, 1st flower, full seed)".
// All adjacent rows become one chart; -99 (not used) values are left out.
bool stageTable(const QStringList &lines, int idx, const QVector<double> &nums,
                const QString &comment, SpeGraphData &out)
{
    if (nums.size() != 3 || comment.isEmpty()) return false;
    QString section;
    for (int i = idx - 1; i >= 0; --i)
        if (lines[i].trimmed().startsWith('*')) { section = lines[i].trimmed(); break; }
    if (!section.contains("PHOSPHORUS", Qt::CaseInsensitive)) return false;

    auto row = [&](int i, QVector<double> &v, QString &com) {
        QString ft;
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith('!') || t.startsWith('*') || t.startsWith('@')) return false;
        parseLine(t, v, ft, com);
        return v.size() == 3 && !com.isEmpty();
    };
    int first = idx, last = idx;
    QVector<double> v; QString com;
    while (first > 0 && row(first - 1, v, com)) --first;
    while (last + 1 < lines.size() && row(last + 1, v, com)) ++last;

    SpeGraphData d;
    d.xTickLabels = {"Stage 1", "Stage 2", "Stage 3"};
    static const QRegularExpression stagesRe("\\(([^)]*,[^)]*)\\)");
    bool ratio = false;
    for (int i = first; i <= last; ++i) {
        row(i, v, com);
        QString label = com.section('(', 0, 0).section('!', 0, 0).trimmed();
        if (label.isEmpty()) label = QString("Row %1").arg(i - first + 1);
        ratio = ratio || label.contains("ratio", Qt::CaseInsensitive);
        const auto m = stagesRe.match(com);
        if (m.hasMatch() && d.xTickLabels.first() == "Stage 1") {
            QStringList st = m.captured(1).split(',');
            if (st.size() == 3) for (int s = 0; s < 3; ++s) d.xTickLabels[s] = st[s].trimmed();
        }
        SpeGraphSeries s;
        s.label = label.left(28);
        for (int k = 0; k < 3; ++k)
            if (v[k] > -98.0) s.points.append({double(k + 1), v[k]});
        if (!s.points.isEmpty()) d.series.append(s);
    }
    if (d.series.isEmpty()) return false;
    d.title = ratio ? "Vegetative N:P ratio by growth stage" : "P concentration by growth stage";
    d.xAxisLabel = "Growth stage";
    d.yAxisLabel = ratio ? "N:P ratio" : "g P / g tissue";
    out = d;
    return true;
}

// Render an @-header columnar table starting at headerIdx.
// Handles: 4-row temp-function tables, 2-col XY tables, multi-col series.
bool atTable(const QStringList &lines, int headerIdx, SpeGraphData &out)
{
    QString hdr = lines[headerIdx].trimmed().mid(1);  // strip '@'
    QStringList cols = tokens(hdr);
    if (cols.size() < 2) return false;

    QVector<QVector<double>> tableData;
    for (int i = headerIdx + 1; i < lines.size(); ++i) {
        QString t = lines[i].trimmed();
        if (t.startsWith('*') || t.startsWith('@')) break;
        if (!t.isEmpty() && !t.startsWith('!')) {
            QVector<double> rowNums; QString ft, com;
            parseLine(t, rowNums, ft, com);
            if (!rowNums.isEmpty()) tableData.append(rowNums);
        }
    }
    if (tableData.isEmpty()) return false;

    int nRows = tableData.size();
    int usableCols = cols.size();
    for (const auto &row : tableData)
        usableCols = std::min(usableCols, (int)row.size());

    // 4 data rows, >=2 cols -> each column is a [Tb,To1,To2,Tm] temp function
    if (nRows == 4 && usableCols >= 2) {
        SpeGraphData data;
        data.xAxisLabel = "Temperature";
        data.yAxisLabel = "Relative Effect";
        int maxCols = std::min(usableCols, 8);
        for (int c = 0; c < maxCols && c < cols.size(); ++c) {
            double tb  = tableData[0][c], to1 = tableData[1][c];
            double to2 = tableData[2][c], tm  = tableData[3][c];
            if (tb > to1 || to1 > to2 || to2 > tm) continue;
            data.series.append(make4ValSeries(cols[c], tb, to1, to2, tm, ""));
        }
        if (!data.series.isEmpty()) {
            data.title = cols[0];
            out = data;
            return true;
        }
    }

    // 2 usable cols, >=2 rows -> simple XY table
    if (usableCols == 2 && nRows >= 2) {
        SpeGraphData data;
        data.title = cols[0] + " / " + cols[1];
        data.xAxisLabel = cols[0];
        data.yAxisLabel = cols[1];
        SpeGraphSeries s; s.label = cols[1];
        for (const auto &row : tableData)
            if ((int)row.size() >= 2) s.points.append({row[0], row[1]});
        if (!s.points.isEmpty()) {
            data.series.append(s);
            out = data;
            return true;
        }
    }

    // >=3 usable cols, >=3 rows -> col0 = X, remaining cols = Y series
    if (usableCols >= 3 && nRows >= 3) {
        SpeGraphData data;
        data.title = cols[0];
        data.xAxisLabel = cols[0];
        data.yAxisLabel = "Value";
        int maxSeries = std::min(usableCols - 1, 4);
        for (int c = 1; c <= maxSeries && c < cols.size(); ++c) {
            SpeGraphSeries s; s.label = cols[c];
            for (const auto &row : tableData)
                if ((int)row.size() > c) s.points.append({row[0], row[c]});
            if (!s.points.isEmpty()) data.series.append(s);
        }
        if (!data.series.isEmpty()) {
            out = data;
            return true;
        }
    }

    return false;
}

// CROPGRO *PLANT COMPOSITION VALUES section as a stacked bar per organ.
bool plantComposition(const QStringList &lines, int idx, const QString &comment,
                      SpeGraphData &out)
{
    // Trigger: comment has 3+ comma-separated param names (not the 2-item X,Y pattern)
    bool hasCompoundParams = false;
    for (const QString &tok : tokens(comment))
        if (tok.count(',') >= 2) { hasCompoundParams = true; break; }
    if (!hasCompoundParams) return false;

    // Walk backward to find !*PLANT COMPOSITION VALUES header
    int header = -1;
    for (int i = idx - 1; i >= 0; --i) {
        QString t = lines[i].trimmed();
        if (t.contains("PLANT COMPOSITION", Qt::CaseInsensitive)) { header = i; break; }
        if (t.startsWith('*') || t.startsWith("!*")) break;
    }
    if (header < 0) return false;

    // Collect all param->value mappings from the section
    QMap<QString, double> pv;
    for (int i = header + 1; i < lines.size(); ++i) {
        QString t = lines[i].trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith('*') || t.startsWith("!*")) break;
        if (t.startsWith('!')) continue;
        QVector<double> lNums; QString lFT, lCom;
        parseLine(t, lNums, lFT, lCom);
        if (lNums.isEmpty()) continue;
        QStringList pNames;
        for (const QString &tok : tokens(lCom))
            pNames << tok.split(',', Qt::SkipEmptyParts);
        for (int i = 0; i < pNames.size() && i < lNums.size(); ++i)
            pv[pNames[i].toUpper()] = lNums[i];
    }
    if (pv.isEmpty()) return false;

    // Per-organ param names [component][organ]: Leaf/Stem/Root/Shell/Seed/Nodule
    static const QStringList ORGANS = {"Leaf","Stem","Root","Shell","Seed","Nodule"};
    static const QVector<QStringList> COMP_PARAMS = {
        {"PROLFF","PROSTF","PRORTF","PROSHF","SDPROS","PRONOD"},  // Protein
        {"PCARLF","PCARST","PCARRT","PCARSH","PCARSD","PCARNO"},  // Carbohydrate
        {"PLIPLF","PLIPST","PLIPRT","PLIPSH","",      "PLIPNO"},  // Lipid
        {"PLIGLF","PLIGST","PLIGRT","PLIGSH","PLIGSD","PLIGNO"},  // Lignin
        {"POALF", "POAST", "POART", "POASH", "POASD", "POANO"},   // Org. Acid
        {"PMINLF","PMINST","PMINRT","PMINSH","PMINSD","PMINNO"},  // Mineral
    };
    static const QStringList COMP_LABELS =
        {"Protein","Carbohydrate","Lipid","Lignin","Org. Acid","Mineral"};

    SpeGraphData data;
    data.type = SpeGraphType::StackedBar;
    data.title = "Plant Composition";
    data.xAxisLabel = "Organ";
    data.yAxisLabel = "Fraction";
    data.barCategories = ORGANS;
    for (int ci = 0; ci < COMP_PARAMS.size(); ++ci) {
        SpeGraphSeries s;
        s.label = COMP_LABELS[ci];
        for (int oi = 0; oi < ORGANS.size(); ++oi) {
            const QString &pn = COMP_PARAMS[ci][oi];
            double val = pn.isEmpty() ? 0.0 : pv.value(pn, 0.0);
            s.points.append(QPointF(oi, val));
        }
        data.series.append(s);
    }
    out = data;
    return true;
}

} // namespace

bool SpeGraphBuilder::build(const QStringList &lines, int idx, SpeGraphData &out)
{
    if (idx < 0 || idx >= lines.size()) return false;
    const QString trimmed = lines[idx].trimmed();

    // Skip empty lines, standalone comments, and section headers (*).
    if (trimmed.isEmpty() || trimmed.startsWith('!') || trimmed.startsWith('*'))
        return false;
    if (trimmed.startsWith('@'))
        return atTable(lines, idx, out);

    QVector<double> nums;
    QString funcType, comment;
    QString keyword = parseLine(trimmed, nums, funcType, comment);
    if (nums.size() < 2) return false;

    // ── Case 1: DSSAT CURV response function: 4 values + type (LIN, QDR, INL, ...)
    if (nums.size() >= 4 && !funcType.isEmpty()) {
        QString name = keyword.isEmpty() ? paramFromComment(comment) : keyword;
        if (name.isEmpty()) name = "Response";
        const QString desc = curvDescription(comment);
        out = {};
        out.title      = name + "  [" + funcType + "]" + (desc.isEmpty() ? "" : "  " + desc);
        out.xAxisLabel = "Input value";
        out.yAxisLabel = "Relative effect";
        out.series.append(curvSeries(name, funcType, nums[0], nums[1], nums[2], nums[3]));
        return true;
    }
    // CERES-style "PRFTC  6.2  16.5  33.0  44.0": keyword + increasing cardinal temperatures,
    // unless it is the X array of a Y array on the next line (SUBSTOR SENST / SENSF)
    auto xOfNextLine = [&]() {
        const int j = nextDataLine(lines, idx, +1);
        if (j < 0) return false;
        QVector<double> o; QString ft, com;
        const QString kw = parseLine(lines[j].trimmed(), o, ft, com);
        return !kw.isEmpty() && o.size() == nums.size() && !increasing(o, o.size());
    };
    if (nums.size() == 4 && !keyword.isEmpty() && increasing(nums, 4) && !xOfNextLine()) {
        out = {};
        out.title      = keyword;
        out.xAxisLabel = "Temperature";
        out.yAxisLabel = "Relative effect";
        out.series.append(make4ValSeries(keyword, nums[0], nums[1], nums[2], nums[3], ""));
        return true;
    }

    // ── Case 2: interleaved X,Y on one line  (e.g. XRTFAC,YRTFAC) ───────────
    {
        QString xName, yName;
        for (const QString &tok : tokens(comment)) {
            if (tok.contains(',')) {
                QStringList sub = tok.split(',');
                if (sub.size() == 2 &&
                    sub[0].startsWith('X', Qt::CaseInsensitive) &&
                    sub[1].startsWith('Y', Qt::CaseInsensitive)) {
                    xName = sub[0]; yName = sub[1]; break;
                }
            }
        }
        if (!xName.isEmpty() && nums.size() >= 4 && nums.size() % 2 == 0) {
            out = {};
            out.title      = xName + " / " + yName;
            out.xAxisLabel = xName;
            out.yAxisLabel = yName;
            SpeGraphSeries s; s.label = yName;
            for (int i = 0; i + 1 < nums.size(); i += 2)
                s.points.append({nums[i], nums[i + 1]});
            out.series.append(s);
            return true;
        }
    }

    // ── Case B: CERES keyword X/Y pair  (e.g. CO2X / CO2Y) ──────────────────
    if (!keyword.isEmpty()) {
        QString kUpper = keyword.toUpper();
        const bool isX = kUpper.endsWith('X'), isY = kUpper.endsWith('Y');
        if (isX || isY) {
            const QString other = keyword.left(keyword.size() - 1) + (isX ? 'Y' : 'X');
            const int j = nextDataLine(lines, idx, isX ? +1 : -1);
            if (j >= 0) {
                QVector<double> oNums; QString oFT, oCom;
                QString oKw = parseLine(lines[j].trimmed(), oNums, oFT, oCom);
                if (oKw.toUpper() == other.toUpper() && oNums.size() == nums.size()) {
                    const QString xKw = isX ? keyword : oKw, yKw = isX ? oKw : keyword;
                    const QVector<double> &xs = isX ? nums : oNums, &ys = isX ? oNums : nums;
                    out = {};
                    out.title = xKw + " / " + yKw;
                    out.xAxisLabel = xKw; out.yAxisLabel = yKw;
                    SpeGraphSeries s; s.label = yKw;
                    for (int i = 0; i < xs.size(); ++i) s.points.append({xs[i], ys[i]});
                    out.series.append(s);
                    return true;
                }
            }
        }
    }

    // ── Case K: keyword arrays (XLMAXT/YLMAXT, DTTPT/DTTF, istageno/dc_code) ─
    if (!keyword.isEmpty() && keywordArrays(lines, idx, keyword, nums, out)) return true;

    // ── Case 3: Y-line clicked — look backward for matching X-line ───────────
    {
        QString yName = axisName(comment, 'Y');
        if (!yName.isEmpty()) {
            const int j = nextDataLine(lines, idx, -1);
            if (j >= 0) {
                QVector<double> xNums; QString xFT, xCom;
                parseLine(lines[j].trimmed(), xNums, xFT, xCom);
                if (xNums.size() == nums.size()) {
                    QString xName = axisName(xCom, 'X');
                    out = {};
                    out.title      = yName;
                    out.xAxisLabel = xName.isEmpty() ? "X" : xName;
                    out.yAxisLabel = yName;
                    SpeGraphSeries s; s.label = yName;
                    for (int i = 0; i < xNums.size(); ++i)
                        s.points.append({xNums[i], nums[i]});
                    out.series.append(s);
                    return true;
                }
            }
        }
    }

    // ── Case 4: X-line — look forward for up to 2 Y-lines (CROPGRO style) ───
    {
        QString xName = axisName(comment, 'X');
        if (!xName.isEmpty()) {
            QString title = paramFromComment(comment);
            SpeGraphData data;
            data.title      = title.isEmpty() ? xName : title;
            data.xAxisLabel = xName;
            data.yAxisLabel = "Y";

            // Try to add a Y-line as a new series; returns true on success.
            auto addYLine = [&](const QString &lineText) -> bool {
                QVector<double> yNums; QString yFT, yCom;
                parseLine(lineText, yNums, yFT, yCom);
                if (yNums.size() != nums.size()) return false;
                QString yName = axisName(yCom, 'Y');
                SpeGraphSeries s;
                s.label = yName.isEmpty() ? "Y" : yName;
                for (int i = 0; i < nums.size(); ++i)
                    s.points.append({nums[i], yNums[i]});
                data.series.append(s);
                if (!yName.isEmpty() && data.yAxisLabel == "Y") data.yAxisLabel = yName;
                return true;
            };

            const int b1 = nextDataLine(lines, idx, +1);
            if (b1 >= 0 && addYLine(lines[b1].trimmed())) {
                const int b2 = nextDataLine(lines, b1, +1);
                if (b2 >= 0) {
                    QVector<double> y2; QString y2ft, y2com;
                    parseLine(lines[b2].trimmed(), y2, y2ft, y2com);
                    if (!axisName(y2com, 'Y').isEmpty()) addYLine(lines[b2].trimmed());
                }
                out = data;
                return true;
            }
        }
    }

    // ── Arrays named in a dotted comment (XSTAGE/SENPOR, XSENMX/SENMAX) ─────
    if (dottedArrays(lines, idx, nums, out)) return true;

    // ── Rows under a "TB TO1 TO2 TM" comment header ─────────────────────────
    if (cardinalTable(lines, idx, out)) return true;

    // ── Values at three growth stages (*PHOSPHORUS CONTENT) ─────────────────
    if (stageTable(lines, idx, nums, comment, out)) return true;

    // ── Case D: CROPGRO *PLANT COMPOSITION VALUES stacked bar ────────────────
    if (plantComposition(lines, idx, comment, out)) return true;

    // ── Case 5: unlabeled data row — walk backward for @-header table ─────────
    for (int i = idx - 1; i >= 0; --i) {
        QString t = lines[i].trimmed();
        if (t.startsWith('*')) break;
        if (t.startsWith('@')) return atTable(lines, i, out);
    }
    return false;
}
