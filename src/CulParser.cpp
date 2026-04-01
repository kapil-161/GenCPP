#include "CulParser.h"
#include "Config.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>

QString CulParser::formatParam(double value, const ParamFormat &fmt)
{
    if (fmt.trailingDot) {
        // e.g. " 380." - integer + "." right-justified
        QString s = QString::number(qRound(value)) + ".";
        return " " + s.rightJustified(fmt.width, ' ');
    }
    return " " + QString("%1").arg(value, fmt.width, 'f', fmt.decimals);
}

QVector<ParamFormat> CulParser::inferFormats(const QVector<CulRow> &rows, int numParams)
{
    QVector<ParamFormat> formats(numParams);
    
    for (int p = 0; p < numParams; ++p) {
        bool found = false;
        
        // Prefer MINIMA/MAXIMA rows to establish the standard decimal format
        for (const auto &r : rows) {
            if (r.isMinMax && p < r.paramStrs.size() && !r.paramStrs[p].isEmpty()) {
                QString s = r.paramStrs[p].trimmed();
                if (s.endsWith('.')) {
                    formats[p].trailingDot = true;
                    formats[p].decimals = 0;
                } else {
                    int dotIdx = s.indexOf('.');
                    formats[p].decimals = (dotIdx >= 0) ? s.length() - dotIdx - 1 : 0;
                }
                found = true;
                break;
            }
        }
        
        // Fallback to any valid row if no MINIMA/MAXIMA found
        if (!found) {
            for (const auto &r : rows) {
                if (p < r.paramStrs.size() && !r.paramStrs[p].isEmpty()) {
                    QString s = r.paramStrs[p].trimmed();
                    if (s.endsWith('.')) {
                        formats[p].trailingDot = true;
                        formats[p].decimals = 0;
                    } else {
                        int dotIdx = s.indexOf('.');
                        formats[p].decimals = (dotIdx >= 0) ? s.length() - dotIdx - 1 : 0;
                    }
                    break;
                }
            }
        }
        
        // Width will be 5, plus 1 space prefix = 6 (DSSAT standard data width)
        formats[p].width = 5;
    }
    
    return formats;
}

QStringList CulParser::extractParamNames(const QStringList &headerLines)
{
    QStringList names;
    for (const QString &line : headerLines) {
        if (line.startsWith("@VAR#") || line.startsWith("@ VAR#")) {
            int ecoIdx = line.indexOf("ECO#");
            if (ecoIdx >= 0) {
                QString paramStr = line.mid(ecoIdx + 4);
                names = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            } else {
                // Try from EXPNO if ECO# is missing
                int expIdx = line.indexOf("EXPNO");
                if (expIdx >= 0) {
                    QString paramStr = line.mid(expIdx + 5);
                    names = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                }
            }
            break;
        }
    }
    return names;
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
        
        // Extract ECO# from fixed position 30-35 (DSSAT fixed-width format)
        row.ecoNum = line.mid(30, 6).trimmed();
        if (row.ecoNum.isEmpty()) continue;  // Invalid row if no ECO#
        
        // VRNAME is in positions 7-29, followed by optional EXPNO
        QString vrAndExp = line.mid(7, 23);  // 23 chars from pos 7-29
        
        // Extract EXPNO: look for single digit or "." from right side of vrAndExp
        // (usually near the end before the ECO# at position 30)
        QString expNo;
        for (int i = vrAndExp.length() - 1; i >= 0; --i) {
            QChar c = vrAndExp[i];
            if (c == '.') {
                expNo = ".";
                vrAndExp = vrAndExp.left(i).trimmed();
                break;
            } else if (c.isDigit()) {
                expNo = c;
                vrAndExp = vrAndExp.left(i).trimmed();
                break;
            }
        }
        
        row.vrName = vrAndExp;  // Remaining is the cultivar name
        row.expNo = expNo.isEmpty() ? " " : expNo;
        
        // Parameters start at position 36 onwards
        QString paramStr = line.mid(36);
        QStringList tokens = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        
        // Parse all parameter tokens dynamically without hard limit
        for (int i = 0; i < tokens.size(); ++i) {
            row.params << std::optional<double>(tokens[i].toDouble());
            row.paramStrs << tokens[i];
        }

        row.isMinMax = (row.varNum == "999991" || row.varNum == "999992");
        rows << row;
    }

    return rows;
}

QString CulParser::formatRow(const CulRow &row, const QVector<ParamFormat> &formats, int numParams)
{
    // Fixed-width format from spec:
    // "%-6s %-13s%1s       . %-6s " + formatted params
    QString line;
    line += row.varNum.leftJustified(6, ' ');
    line += ' ';
    line += row.vrName.leftJustified(13, ' ');
    line += row.expNo.rightJustified(1, ' ');
    line += "       . ";
    line += row.ecoNum.leftJustified(6, ' ');

    int actualParams = std::max(numParams, static_cast<int>(row.params.size()));

    for (int i = 0; i < actualParams; ++i) {
        if (i < row.paramStrs.size() && !row.paramStrs[i].isEmpty()) {
            double v = (i < row.params.size() && row.params[i].has_value()) ? row.params[i].value() : 0.0;
            line += formatParam(v, (i < formats.size()) ? formats[i] : ParamFormat());
        } else {
            // Edited, modified, or appended values
            double v = (i < row.params.size() && row.params[i].has_value()) ? row.params[i].value() : 0.0;
            line += formatParam(v, (i < formats.size()) ? formats[i] : ParamFormat());
        }
    }
    return line;
}

bool CulParser::write(const QString &filePath,
                      const QVector<CulRow> &rows,
                      const QStringList &headerLines,
                      const QStringList &paramNames)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Latin1);

    // Write header lines first
    for (const QString &h : headerLines) {
        out << h << "\n";
    }

    // Pre-infer formats for exact rewriting dynamically
    int numParams = paramNames.size();
    if (numParams == 0) {
        for (const auto &r : rows) {
            if (r.params.size() > numParams) numParams = r.params.size();
        }
    }
    QVector<ParamFormat> formats = inferFormats(rows, numParams);

    // Write data rows
    for (const CulRow &row : rows) {
        out << formatRow(row, formats, numParams) << "\n";
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
    
    // Try fixed-width extraction first (for DSSAT format files)
    row.ecoNum = line.mid(30, 6).trimmed();
    
    if (!row.ecoNum.isEmpty()) {
        // Fixed-width format: ECO# is at position 30-35
        QString vrAndExp = line.mid(7, 23);
        
        // Extract EXPNO from right side
        QString expNo;
        for (int i = vrAndExp.length() - 1; i >= 0; --i) {
            QChar c = vrAndExp[i];
            if (c == '.') {
                expNo = ".";
                vrAndExp = vrAndExp.left(i).trimmed();
                break;
            } else if (c.isDigit()) {
                expNo = c;
                vrAndExp = vrAndExp.left(i).trimmed();
                break;
            }
        }
        
        row.vrName = vrAndExp;
        row.expNo = expNo.isEmpty() ? " " : expNo;
        
        // Parameters from position 36
        QString paramStr = line.mid(36);
        QStringList tokens = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 0; i < tokens.size(); ++i) {
            row.params << std::optional<double>(tokens[i].toDouble());
            row.paramStrs << tokens[i];
        }
    } else {
        // Fallback: token-based parsing for non-fixed-width formats
        row.vrName = line.mid(7, 13).trimmed();
        
        QString rest = line.mid(20);
        QStringList tokens = rest.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        
        if (tokens.isEmpty()) { row.varNum.clear(); return row; }
        if (tokens.size() < 2) { row.varNum.clear(); return row; }
        
        if (tokens[0] == ".") {
            row.expNo = ".";
        } else {
            row.expNo = tokens[0];
        }
        
        row.ecoNum = tokens[1];
        
        for (int i = 2; i < tokens.size(); ++i) {
            row.params << std::optional<double>(tokens[i].toDouble());
            row.paramStrs << tokens[i];
        }
    }
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
