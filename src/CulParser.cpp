#include "CulParser.h"
#include "Config.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>

QString CulParser::formatParam(double value, const ParamFormat &fmt)
{
    // Result is always exactly width+1 chars: a separating space, then the value
    // right-aligned in `width` columns. If the value is too wide, drop the leading
    // zero ("-0.006" -> "-.006", DSSAT style), then decimals, until it fits.
    QString s;
    if (fmt.trailingDot) {
        // e.g. " 380." - integer + "." right-justified
        s = QString::number(qRound(value)) + ".";
        if (s.size() > fmt.width) s.chop(1);   // "10000." -> "10000"
    } else {
        for (int d = fmt.decimals; d >= 0; --d) {
            s = QString::number(value, 'f', d);
            if (s.startsWith("0.") && s.size() > fmt.width)  s.remove(0, 1);
            if (s.startsWith("-0.") && s.size() > fmt.width) s.remove(1, 1);
            if (s.size() <= fmt.width) break;
        }
    }
    if (s.size() > fmt.width)
        s = QString(fmt.width, '*');   // Fortran-style overflow rather than shifting columns
    return " " + s.rightJustified(fmt.width, ' ');
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
        
        // Determine field width dynamically from rows
        int fWidth = 6;
        for (const auto &r : rows) {
            if (r.fWidth > fWidth) fWidth = r.fWidth;
        }

        // Width will be fWidth - 1, plus 1 space prefix = fWidth (DSSAT standard data width)
        formats[p].width = fWidth - 1;
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

QVector<CulRow> CulParser::parse(const QString &filePath, QStringList &headerLines,
                                 QStringList *footerLines)
{
    QVector<CulRow> rows;
    headerLines.clear();
    if (footerLines) footerLines->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);
    // Byte-exact Latin1 round-trip: don't let a UTF-8 BOM switch the decoder,
    // or non-Latin1 characters (e.g. "˚Cd") are written back as '?'.
    // The BOM itself is not valid DSSAT input and is dropped.
    in.setAutoDetectUnicode(false);
    static const QString utf8Bom = QString::fromLatin1("\xEF\xBB\xBF");

    // Comment and structural lines (* ! @ $) and blank lines keep their place:
    // before the first data row they go to headerLines (so "!Calibration",
    // "!Coefficient #", unit lines etc. keep their order); between data rows
    // (GLUE history, extra "@VAR#" section headers) they are written back just
    // before the next row (preComment); after the last row they become footerLines.
    // Data rows are always rewritten in fixed-width format; lines that are
    // not valid fixed-width data rows are dropped.
    bool inDataSection = false;
    QStringList pending;
    QVector<QVector<int>> tokenEnds;   // per row: end of each value, relative to col 36

    while (!in.atEnd()) {
        QString line = in.readLine();
        // Remove Windows \r if present
        if (line.endsWith('\r')) line.chop(1);
        if (headerLines.isEmpty() && rows.isEmpty() && line.startsWith(utf8Bom))
            line.remove(0, utf8Bom.size());

        if (line.trimmed().isEmpty()) {
            if (!inDataSection)
                headerLines << QString();
            else if (pending.isEmpty())
                ++rows.last().trailingBlanks;
            else
                pending << QString();          // blank line inside a comment block
            continue;
        }

        if (QString("*!@$").contains(line[0])) {
            if (inDataSection) pending << line; else headerLines << line;
            continue;
        }

        // Data line: must be long enough
        if (line.length() < 36) continue;

        CulRow row;
        row.varNum = line.left(6).trimmed();
        row.vrName = line.mid(7, 16).trimmed();

        // Positions 23-29: 7-char experiment region (e.g. "   1,6 " or "      6")
        row.expNo = line.mid(23, 7);

        // Extract ECO# from strictly position 30-35 (A6)
        row.ecoNum = line.mid(30, 6).trimmed();
        if (row.ecoNum.isEmpty()) continue;  // Invalid row if no ECO#
        inDataSection = true;

        // Parameters start at position 36 onwards; remember where each value ends
        static const QRegularExpression tokRe("\\S+");
        QVector<int> ends;
        for (auto it = tokRe.globalMatch(line, 36); it.hasNext(); ) {
            const auto m = it.next();
            row.params << std::optional<double>(m.captured().toDouble());
            row.paramStrs << m.captured();
            ends << int(m.capturedEnd()) - 36;
        }
        tokenEnds << ends;
        // VAR# longer than A6, VRNAME running past A16 into EXPNO, or EXPNO
        // running into ECO# (positions 6 and 29 must be blank)
        if (line[6] != ' ' || line[29] != ' ' || (line[22] != ' ' && line[23] != ' '))
            row.misaligned = true;

        row.isMinMax = (row.varNum == "999991" || row.varNum == "999992");
        row.preComment = pending.join('\n');
        pending.clear();
        rows << row;
    }

    // Comments after the last data row have no row to attach to — keep them as a footer
    if (footerLines)
        *footerLines = pending;

    // Field width: the width most values have (DSSAT CUL fields are uniform,
    // usually 6). Rows whose values don't end on the column edges — e.g. a value
    // shifted by one, or a blank field that shifts the values after it — are flagged.
    QMap<int, int> widthVotes;
    for (const auto &e : tokenEnds)
        for (int i = 0; i < e.size(); ++i)
            ++widthVotes[i == 0 ? e[0] : e[i] - e[i - 1]];
    int width = 6, best = -1;
    for (auto it = widthVotes.begin(); it != widthVotes.end(); ++it)
        if (it.value() > best) { best = it.value(); width = it.key(); }
    if (width < 2) width = 6;
    for (int r = 0; r < rows.size(); ++r) {
        rows[r].fWidth = width;
        for (int i = 0; i < tokenEnds[r].size(); ++i)
            if (tokenEnds[r][i] != (i + 1) * width) { rows[r].misaligned = true; break; }
    }

    return rows;
}

QString CulParser::formatRow(const CulRow &row, const QVector<ParamFormat> &formats, int numParams)
{
    // Fixed-width format from spec:
    // "%-6s %-13s%1s       . %-6s " + formatted params
    // Every field is truncated to its width so a long value can never shift the columns.
    QString line;
    line += row.varNum.leftJustified(6, ' ', true);
    line += ' ';
    line += row.vrName.leftJustified(16, ' ', true); // strict A16 (positions 7-22)
    // 7X region: 6-char content right-justified + mandatory space at pos 29
    line += row.expNo.trimmed().rightJustified(6, ' ', true) + ' ';
    line += row.ecoNum.leftJustified(6, ' ', true);  // strict A6 (positions 30-35)

    // Only the values this row has: padding a short row with extra values would
    // change what DSSAT reads. A value with no entry is written as missing (-99).
    Q_UNUSED(numParams);
    const int actualParams = static_cast<int>(row.params.size());

    for (int i = 0; i < actualParams; ++i) {
        if (!row.params[i].has_value()) {
            line += QString("-99").rightJustified((i < formats.size() ? formats[i].width : 5) + 1, ' ');
            continue;
        }
        double v = row.params[i].value();
        ParamFormat fmt = (i < formats.size()) ? formats[i] : ParamFormat();
        // The value's own text (as read, typed or pasted) is written as is, right-aligned,
        // when it still matches the value and fits the field: ".405" stays ".405".
        if (i < row.paramStrs.size() && !row.paramStrs[i].trimmed().isEmpty()) {
            const QString s = row.paramStrs[i].trimmed();
            bool ok;
            const double sv = s.toDouble(&ok);
            if (ok && sv == v && s.size() <= fmt.width) {
                line += " " + s.rightJustified(fmt.width, ' ');
                continue;
            }
        }
        // If this cell has its own stored string, derive decimal precision from it
        // (e.g. GLUE-applied values like "39.80" should not be rounded to "40")
        if (i < row.paramStrs.size() && !row.paramStrs[i].isEmpty()) {
            QString s = row.paramStrs[i].trimmed();
            int dotIdx = s.indexOf('.');
            if (dotIdx >= 0) {
                fmt.decimals = s.length() - dotIdx - 1;
                fmt.trailingDot = (fmt.decimals == 0);   // "240." stays "240."
            }
        }
        line += formatParam(v, fmt);
    }
    return line;
}

bool CulParser::write(const QString &filePath,
                      const QVector<CulRow> &rows,
                      const QStringList &headerLines,
                      const QStringList &paramNames,
                      const QStringList &footerLines)
{
    QStringList out;
    auto put = [&](const QString &s) { out << s; };

    // Write header lines first
    for (const QString &h : headerLines)
        put(h);

    // Pre-infer formats for exact rewriting dynamically
    int numParams = paramNames.size();
    if (numParams == 0) {
        for (const auto &r : rows) {
            if (r.params.size() > numParams) numParams = r.params.size();
        }
    }
    QVector<ParamFormat> formats = inferFormats(rows, numParams);

    // Write data rows, preserving inline history comments and blank lines
    for (const CulRow &row : rows) {
        if (!row.preComment.isEmpty())
            put(row.preComment);
        put(formatRow(row, formats, numParams));
        for (int b = 0; b < row.trailingBlanks; ++b)
            put(QString());
    }

    for (const QString &f : footerLines)
        put(f);

    return writeLines(filePath, out);
}

bool CulParser::writeLines(const QString &filePath, const QStringList &lines)
{
    // Keep the line endings of the file being overwritten (DSSAT files are usually
    // CRLF); new files get the platform default.
#ifdef Q_OS_WIN
    QString eol = "\r\n";
#else
    QString eol = "\n";
#endif
    {
        QFile existing(filePath);
        if (existing.open(QIODevice::ReadOnly)) {
            const QByteArray head = existing.read(64 * 1024);
            if (head.contains('\n'))
                eol = head.contains("\r\n") ? "\r\n" : "\n";
        }
    }

    QString text;
    for (const QString &l : lines)
        text += QString(l).replace('\n', eol) + eol;   // a line may hold several (preComment)

    const QByteArray bytes = text.toLatin1();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))   // binary: eol is already in the text
        return false;
    return file.write(bytes) == bytes.size();
}

QString CulParser::nextFreeId(const QString &srcId, const QStringList &existing)
{
    // Letter prefix = leading non-digits; keep at least 2 digits of room in 6 chars
    QString prefix = srcId.trimmed().toUpper();
    int p = 0;
    while (p < prefix.size() && !prefix[p].isDigit()) ++p;
    prefix = prefix.left(qMin(p, 4));
    if (prefix.isEmpty()) prefix = "XX";
    const int digits = 6 - prefix.size();

    int maxNum = 0;
    for (const QString &id : existing) {
        if (!id.startsWith(prefix, Qt::CaseInsensitive) || id.size() != 6) continue;
        bool ok;
        int n = id.mid(prefix.size()).toInt(&ok);
        if (ok && n > maxNum) maxNum = n;
    }

    int limit = 1;
    for (int i = 0; i < digits; ++i) limit *= 10;
    for (int n = maxNum + 1; n < maxNum + limit; ++n) {
        QString id = prefix + QString::number(n % limit).rightJustified(digits, '0');
        if (!existing.contains(id, Qt::CaseInsensitive))
            return id;
    }
    return prefix + QString(digits, '9');
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
    
    // Strict A6, 1X, A16, 7X, A6 extraction
    row.ecoNum = line.mid(30, 6).trimmed();
    
    if (!row.ecoNum.isEmpty()) {
        row.vrName = line.mid(7, 16).trimmed();
        row.expNo = line.mid(23, 7); // preserve full 7-char experiment region
        
        // Parameters from position 36
        QString paramStr = line.mid(36);
        QStringList tokens = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        row.fWidth = tokens.size() > 0 ? qMax(6, (int)std::round(paramStr.length() / (double)tokens.size())) : 6;
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

// Built-in fallback classification for the 18 standard CROPGRO cultivar
// parameters, used only when a file has no "!Calibration" header line of
// its own. Phenology (P) params drive development timing; Growth (G) params
// drive vegetative/reproductive growth; seed-composition traits (SDPRO,
// SDLIP) are typically measured rather than calibrated, so they default to
// "Not calibrated" (N). Any of these can still be overridden per-file via
// the column header right-click menu, which persists the choice by writing
// an explicit "!Calibration" line.
static const QMap<QString, QString> DEFAULT_CALIBRATION_TYPES = {
    {"CSDL",  "P"}, {"PPSEN", "P"}, {"EM-FL", "P"}, {"FL-SH", "P"},
    {"FL-SD", "P"}, {"SD-PM", "P"}, {"FL-LF", "P"},
    {"LFMAX", "G"}, {"SLAVR", "G"}, {"SIZLF", "G"}, {"XFRT",  "G"},
    {"WTPSD", "G"}, {"SFDUR", "G"}, {"SDPDV", "G"}, {"PODUR", "G"},
    {"THRSH", "G"},
    {"SDPRO", "N"}, {"SDLIP", "N"},
};

QMap<QString, QString> CulParser::calibrationTypes(const QStringList &headerLines)
{
    static const QRegularExpression calRe("^!\\s*[Cc]alibration\\b");

    for (const QString &line : headerLines) {
        if (!calRe.match(line).hasMatch()) continue;

        // Split and skip the first token ("!Calibration")
        QMap<QString, QString> types;
        QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 1; i < tokens.size() && (i - 1) < CUL_PARAM_NAMES.size(); ++i)
            types[CUL_PARAM_NAMES[i - 1]] = tokens[i].toUpper();
        return types;
    }

    // No "!Calibration" line in this file yet — seed sensible defaults for
    // the standard parameter set so the P/G/N badges show up without
    // requiring a manual per-column selection first.
    return DEFAULT_CALIBRATION_TYPES;
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
