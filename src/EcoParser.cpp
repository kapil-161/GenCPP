#include "EcoParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>

// All ECO params use 5.2f by default; a few are integer-like
static const struct EcoFmt { int decimals; } ECO_FMTS[16] = {
    {3}, // 0  PP-SS
    {1}, // 1  PL-EM
    {1}, // 2  EM-V1
    {1}, // 3  V1-JU
    {2}, // 4  JU-R0
    {2}, // 5  PM06
    {2}, // 6  PM09
    {2}, // 7  LNHSH
    {1}, // 8  R7-R8
    {1}, // 9  FL-VS
    {3}, // 10 TRIFL
    {2}, // 11 RWDTH
    {2}, // 12 RHGHT
    {3}, // 13 R1PPO
    {1}, // 14 OPTBI
    {3}, // 15 SLOBI
};

QString EcoParser::formatParam(double value, int idx)
{
    if (idx < 0 || idx >= 16) return QString("%1").arg(value, 5);
    return QString("%1").arg(value, 5, 'f', ECO_FMTS[idx].decimals);
}

QVector<EcoRow> EcoParser::parse(const QString &filePath, QStringList &headerLines)
{
    QVector<EcoRow> rows;
    headerLines.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.endsWith('\r')) line.chop(1);

        if (line.isEmpty()) {
            headerLines << line;
            continue;
        }

        QChar first = line[0];
        if (first == '*' || first == '!' || first == '@') {
            headerLines << line;
            continue;
        }

        if (line.length() < 23) continue;

        EcoRow row;
        row.ecoNum  = line.left(6).trimmed();
        row.ecoName = line.mid(7, 16).trimmed();

        // Tokens from position 23
        QStringList tokens = line.mid(23).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.size() < 2) continue;

        row.mg = tokens[0];
        row.tm = tokens[1];
        for (int i = 2; i < tokens.size() && row.params.size() < 16; ++i)
            row.params << tokens[i].toDouble();

        while (row.params.size() < 16) row.params << 0.0;

        row.isMinMax = (row.ecoNum == "999991" || row.ecoNum == "999992");
        rows << row;
    }

    return rows;
}

QString EcoParser::formatRow(const EcoRow &row)
{
    // "%-6s %-16s%-2s %-2s " + 16 params
    QString line;
    line += row.ecoNum.leftJustified(6, ' ');
    line += ' ';
    line += row.ecoName.leftJustified(16, ' ');
    line += row.mg.leftJustified(2, ' ');
    line += ' ';
    line += row.tm.leftJustified(2, ' ');
    line += ' ';

    for (int i = 0; i < 16; ++i) {
        double v = (i < row.params.size()) ? row.params[i] : 0.0;
        line += formatParam(v, i);
        if (i < 15) line += ' ';
    }
    return line;
}

bool EcoParser::write(const QString &filePath,
                      const QVector<EcoRow> &rows,
                      const QStringList &headerLines)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Latin1);

    for (const QString &h : headerLines)
        out << h << "\r\n";

    for (const EcoRow &row : rows)
        out << formatRow(row) << "\r\n";

    return true;
}
