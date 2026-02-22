#include "CulParser.h"
#include "Config.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>

// Numeric format widths for the 18 CUL parameters
// "f"  = floating point, width 5
// Format spec: {precision, useTrailingDot}
static const struct CulFmt { int decimals; bool trailingDot; } CUL_FMTS[18] = {
    {2, false},  // 0  CSDL    5.2f
    {3, false},  // 1  PPSEN   5.3f
    {1, false},  // 2  EM-FL   5.1f
    {1, false},  // 3  FL-SH   5.1f
    {1, false},  // 4  FL-SD   5.1f
    {1, false},  // 5  SD-PM   5.1f
    {1, false},  // 6  FL-LF   5.1f
    {3, false},  // 7  LFMAX   5.3f
    {0, true },  // 8  SLAVR   5.0f  "380."
    {1, false},  // 9  SIZLF   5.1f
    {3, false},  // 10 XFRT    5.3f
    {3, false},  // 11 WTPSD   5.3f
    {1, false},  // 12 SFDUR   5.1f
    {2, false},  // 13 SDPDV   5.2f
    {1, false},  // 14 PODUR   5.1f
    {1, false},  // 15 THRSH   5.1f
    {3, false},  // 16 SDPRO   5.3f
    {3, false},  // 17 SDLIP   5.3f
};

QString CulParser::formatParam(double value, int idx)
{
    if (idx < 0 || idx >= 18) return QString("%1").arg(value, 5);
    const auto &fmt = CUL_FMTS[idx];
    if (fmt.trailingDot) {
        // e.g. " 380." – integer + "." right-justified in 5 chars
        QString s = QString::number(qRound(value)) + ".";
        return s.rightJustified(5, ' ');
    }
    return QString("%1").arg(value, 5, 'f', fmt.decimals);
}

QVector<CulRow> CulParser::parse(const QString &filePath, QStringList &headerLines)
{
    QVector<CulRow> rows;
    headerLines.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);

    while (!in.atEnd()) {
        QString line = in.readLine();
        // Remove Windows \r if present
        if (line.endsWith('\r')) line.chop(1);

        if (line.isEmpty()) {
            headerLines << line;
            continue;
        }

        QChar first = line[0];

        // Skip / preserve header lines
        if (first == '*' || first == '!' || first == '@') {
            headerLines << line;
            continue;
        }

        // Data line: must be long enough
        if (line.length() < 36) continue;

        CulRow row;
        row.varNum = line.left(6).trimmed();
        row.vrName = line.mid(7, 13).trimmed();

        // Parse tokens from position 20 onwards
        QString rest = line.mid(20);
        QStringList tokens = rest.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

        if (tokens.isEmpty()) continue;

        // Both cases have same structure: either . or EXPNO, then ECO#, then 18 params
        if (tokens.size() < 2) continue;

        if (tokens[0] == ".") {
            // MINIMA/MAXIMA row – no EXPNO
            row.expNo  = ".";
        } else {
            // Regular cultivar row with EXPNO
            row.expNo  = tokens[0];
        }

        // ecoNum is always tokens[1]
        row.ecoNum = tokens[1];

        // Parameters always start at tokens[2]
        for (int i = 2; i < tokens.size() && row.params.size() < 18; ++i)
            row.params << tokens[i].toDouble();

        // Pad params to 18 if file is short
        while (row.params.size() < 18) row.params << 0.0;

        row.isMinMax = (row.varNum == "999991" || row.varNum == "999992");
        rows << row;
    }

    return rows;
}

QString CulParser::formatRow(const CulRow &row)
{
    // Fixed-width format from spec:
    // "%-6s %-13s%1s       . %-6s " + 18 formatted params
    QString line;
    line += row.varNum.leftJustified(6, ' ');
    line += ' ';
    line += row.vrName.leftJustified(13, ' ');
    line += row.expNo.rightJustified(1, ' ');
    line += "       . ";
    line += row.ecoNum.leftJustified(6, ' ');
    line += ' ';

    for (int i = 0; i < 18; ++i) {
        double v = (i < row.params.size()) ? row.params[i] : 0.0;
        line += formatParam(v, i);
        if (i < 17) line += ' ';
    }
    return line;
}

bool CulParser::write(const QString &filePath,
                      const QVector<CulRow> &rows,
                      const QStringList &headerLines)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Latin1);

    // Write header lines first
    for (const QString &h : headerLines) {
        out << h << "\r\n";
    }

    // Write data rows
    for (const CulRow &row : rows) {
        out << formatRow(row) << "\r\n";
    }

    return true;
}

CulRow CulParser::parseLine(const QString &rawLine)
{
    CulRow row;
    QString line = rawLine.trimmed();
    if (line.isEmpty() || line[0] == '*' || line[0] == '!' || line[0] == '@')
        return row;  // varNum will be empty → caller treats as failure

    // Pad to minimum length so mid() calls are safe
    if (line.length() < 36)
        return row;

    row.varNum = line.left(6).trimmed();
    row.vrName = line.mid(7, 13).trimmed();

    QString rest = line.mid(20);
    QStringList tokens = rest.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    if (tokens.isEmpty()) { row.varNum.clear(); return row; }
    if (tokens.size() < 2) { row.varNum.clear(); return row; }

    if (tokens[0] == ".") {
        // MINIMA/MAXIMA row – no EXPNO
        row.expNo  = ".";
    } else {
        // Regular cultivar row with EXPNO
        row.expNo  = tokens[0];
    }

    // ecoNum is always tokens[1]
    row.ecoNum = tokens[1];

    // Parameters always start at tokens[2]
    for (int i = 2; i < tokens.size() && row.params.size() < 18; ++i)
        row.params << tokens[i].toDouble();
    while (row.params.size() < 18) row.params << 0.0;

    row.isMinMax = (row.varNum == "999991" || row.varNum == "999992");
    return row;
}

QMap<QString, QString> CulParser::calibrationTypes(const QStringList &headerLines)
{
    QMap<QString, QString> types;
    static const QRegularExpression calRe("^!\\s*[Cc]alibration\\b");

    for (const QString &line : headerLines) {
        if (!calRe.match(line).hasMatch()) continue;

        // Split and skip the first token ("!Calibration")
        QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 1; i < tokens.size() && (i - 1) < CUL_PARAM_NAMES.size(); ++i)
            types[CUL_PARAM_NAMES[i - 1]] = tokens[i].toUpper();
        break;
    }
    return types;
}

QMap<QString, QString> CulParser::tooltipsFromHeader(const QStringList &headerLines)
{
    QMap<QString, QString> tips;

    // Entry:       "! KEYWORD  description"  — 1-5 spaces after !, then uppercase keyword
    // Continuation:"!       more text"       — 6+ spaces after ! (aligned with description)
    static const QRegularExpression entryRe(
        "^![ \\t]{1,5}([A-Z][A-Z0-9#/\\-]*)[ \\t]+(\\S.+)$");
    static const QRegularExpression contRe(
        "^![ \\t]{6,}(\\S.+)$");

    bool inDefs = false;
    QString curKey;

    for (const QString &line : headerLines) {
        // Stop at the data header line
        if (line.startsWith('@')) break;

        // Detect the "! COEFF ... DEFINITIONS" section start
        if (!inDefs) {
            if (line.contains("DEFINITIONS", Qt::CaseInsensitive) &&
                line.contains("COEFF", Qt::CaseInsensitive))
                inDefs = true;
            continue;
        }

        QRegularExpressionMatch m = entryRe.match(line);
        if (m.hasMatch()) {
            curKey = m.captured(1);
            tips[curKey] = m.captured(2).trimmed();
        } else if (!curKey.isEmpty()) {
            QRegularExpressionMatch mc = contRe.match(line);
            if (mc.hasMatch())
                tips[curKey] += " " + mc.captured(1).trimmed();
            else
                curKey.clear();  // blank or separator — end of this entry
        }
    }

    return tips;
}
