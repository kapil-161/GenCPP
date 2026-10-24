#include "EcoParser.h"
#include "CulParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QMap>
#include <algorithm>

// Whitespace-separated tokens of line from position `from`, with their exclusive end positions
static QVector<QPair<QString, int>> tokensFrom(const QString &line, int from)
{
    QVector<QPair<QString, int>> out;
    static const QRegularExpression tokRe("\\S+");
    auto it = tokRe.globalMatch(line, from);
    while (it.hasNext()) {
        auto m = it.next();
        out.append({m.captured(), int(m.capturedEnd())});
    }
    return out;
}

static int decimalsOf(const QString &s)
{
    int dot = s.indexOf('.');
    return dot < 0 ? 0 : s.size() - dot - 1;
}

QVector<EcoRow> EcoParser::parse(const QString &filePath, QStringList &headerLines,
                                 QStringList *footerLines, EcoLayout *layoutOut)
{
    QVector<EcoRow> rows;
    headerLines.clear();
    if (footerLines) footerLines->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);
    in.setAutoDetectUnicode(false);   // same rules as CulParser::parse
    static const QString utf8Bom = QString::fromLatin1("\xEF\xBB\xBF");

    EcoLayout layout;
    bool haveAtHeader = false;
    QString atHeader;
    bool inDataSection = false;
    QStringList pending;
    QVector<QVector<QPair<QString, int>>> rowTokens;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.endsWith('\r')) line.chop(1);
        if (headerLines.isEmpty() && rows.isEmpty() && line.startsWith(utf8Bom))
            line.remove(0, utf8Bom.size());

        if (line.trimmed().isEmpty()) {
            if (!inDataSection)        headerLines << QString();
            else if (pending.isEmpty()) ++rows.last().trailingBlanks;
            else                        pending << QString();
            continue;
        }

        if (QString("*!@$").contains(line[0])) {
            if (line[0] == '@' && !haveAtHeader) {
                haveAtHeader = true;
                atHeader = line;
                const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                layout.hasName = t.size() > 1 && (t[1].startsWith("ECONAME") || t[1].startsWith("ECO-NAME"));
                layout.dataStart = layout.hasName ? 24 : 6;
                layout.names = t.mid(layout.hasName ? 2 : 1);
            }
            if (inDataSection) pending << line; else headerLines << line;
            continue;
        }

        // Data row: A6 ECO#, [1X, A16 ECONAME, 1X,] then the columns.
        // Lines that are not valid fixed-width data rows are dropped.
        if (!haveAtHeader || line.size() <= layout.dataStart) continue;
        EcoRow row;
        row.ecoNum = line.left(6).trimmed();
        if (row.ecoNum.isEmpty()) continue;
        if (layout.hasName) row.ecoName = line.mid(7, 16).trimmed();
        const auto toks = tokensFrom(line, layout.dataStart);
        if (toks.isEmpty()) continue;
        row.isMinMax = (row.ecoNum == "999991" || row.ecoNum == "999992");
        // ECO# longer than A6, or ECONAME running past A16 into the 1X separator
        if (line[6] != ' ' || (layout.hasName && line.size() > 23 && line[23] != ' '))
            row.misaligned = true;
        row.preComment = pending.join('\n');
        pending.clear();
        inDataSection = true;
        rows << row;
        rowTokens << toks;
    }
    if (footerLines)
        *footerLines = pending;

    // Column layout: each column's right edge is where the well-formed rows end it.
    // Only rows with the usual number of values vote, so a malformed row (e.g. an
    // over-long name spilling into the data area) can't move the columns.
    QMap<int, int> countVotes;
    for (const auto &t : rowTokens) ++countVotes[t.size()];
    int usualCount = 0, bestCount = -1;
    for (auto it = countVotes.begin(); it != countVotes.end(); ++it)
        if (it.value() > bestCount) { bestCount = it.value(); usualCount = it.key(); }

    const int nCols = qMax(int(layout.names.size()), usualCount);
    while (layout.names.size() < nCols) layout.names << QString("C%1").arg(layout.names.size() + 1);
    const auto headerToks = tokensFrom(atHeader, layout.dataStart);
    for (int c = 0; c < nCols; ++c) {
        QMap<int, int> endVotes;
        for (const auto &t : rowTokens)
            if (t.size() == usualCount && c < t.size()) ++endVotes[t[c].second];
        int end = 0, best = -1;
        for (auto it = endVotes.begin(); it != endVotes.end(); ++it)
            if (it.value() > best) { best = it.value(); end = it.key(); }
        if (best < 0)   // no data for this column: fall back to the header label's right edge
            end = c < headerToks.size() ? headerToks[c].second : (c ? layout.ends[c - 1] : layout.dataStart) + 6;
        const int prev = c ? layout.ends[c - 1] : layout.dataStart;
        if (end < prev + 2) end = prev + 2;   // at least 1 space + 1 char
        layout.ends << end;
    }
    // Values right of the last column (some rows carry extra trailing values) get columns too
    {
        QVector<int> extra;
        for (const auto &t : rowTokens)
            for (const auto &tok : t)
                if (tok.second > layout.ends.last() && !extra.contains(tok.second)) extra << tok.second;
        std::sort(extra.begin(), extra.end());
        for (int e : extra) {
            if (e < layout.ends.last() + 2) continue;
            layout.ends << e;
            layout.names << QString("C%1").arg(layout.names.size() + 1);
        }
    }

    // Values go to the column whose span contains their right end — the way DSSAT
    // reads them. A value not ending exactly on its column's edge, or two values in
    // one column, means the row is not in fixed-width format: flag it (the last
    // value in a column wins).
    for (int r = 0; r < rows.size(); ++r) {
        QStringList vals;
        for (const auto &t : rowTokens[r]) {
            int c = 0;
            while (c < layout.ends.size() && t.second > layout.ends[c]) ++c;
            if (c >= layout.ends.size()) { rows[r].misaligned = true; continue; }
            while (vals.size() <= c) vals << QString();
            if (t.second != layout.ends[c] || !vals[c].isEmpty()) rows[r].misaligned = true;
            vals[c] = t.first;
        }
        if (vals.contains(QString())) rows[r].misaligned = true;   // a column in the middle is blank
        rows[r].vals = vals;
    }

    for (int c = 0; c < layout.ends.size(); ++c) {
        QMap<int, int> decVotes;
        int numericCount = 0, total = 0;
        for (const auto &row : rows) {
            if (c >= row.vals.size() || row.vals[c].isEmpty()) continue;
            ++total;
            bool ok;
            row.vals[c].toDouble(&ok);
            if (ok) { ++numericCount; ++decVotes[decimalsOf(row.vals[c])]; }
        }
        layout.numeric << (total == 0 || numericCount * 2 > total);
        int dec = 0, best = -1;
        for (auto it = decVotes.begin(); it != decVotes.end(); ++it)
            if (it.value() > best) { best = it.value(); dec = it.key(); }
        layout.decimals << dec;
    }

    if (layoutOut) *layoutOut = layout;
    return rows;
}

QString EcoParser::formatRow(const EcoRow &row, const EcoLayout &layout)
{
    QString line = row.ecoNum.leftJustified(6, ' ', true);   // A6 (0-5)
    if (layout.hasName) {
        line += ' ';                                          // 1X (6)
        line += row.ecoName.leftJustified(16, ' ', true);     // A16 (7-22)
    }
    line = line.leftJustified(layout.dataStart, ' ', true);

    // Only the columns this row has: header names beyond the data are optional
    // trailing parameters, and adding -99 for them would change what DSSAT reads.
    const int n = qMin(int(row.vals.size()), int(layout.ends.size()));
    for (int c = 0; c < n; ++c) {
        const int w = layout.width(c);
        QString v = row.vals[c].trimmed();
        if (v.isEmpty()) v = layout.numeric[c] ? "-99" : ".";
        if (v.size() > w - 1) {
            bool ok;
            const double d = v.toDouble(&ok);
            if (ok) {
                // Too wide: let CulParser::formatParam shrink it (drop leading zero/decimals)
                ParamFormat f;
                f.width = w - 1;
                f.decimals = decimalsOf(v);
                f.trailingDot = v.endsWith('.');
                line += CulParser::formatParam(d, f);
                continue;
            }
            v = v.left(w - 1);
        }
        line += v.rightJustified(w, ' ');                     // right-aligned, >= 1 leading space
    }
    return line;
}

bool EcoParser::write(const QString &filePath,
                      const QVector<EcoRow> &rows,
                      const QStringList &headerLines,
                      const EcoLayout &layout,
                      const QStringList &footerLines)
{
    QStringList out = headerLines;
    for (const EcoRow &row : rows) {
        if (!row.preComment.isEmpty())
            out << row.preComment;
        out << formatRow(row, layout);
        for (int b = 0; b < row.trailingBlanks; ++b)
            out << QString();
    }
    out << footerLines;
    return CulParser::writeLines(filePath, out);
}
