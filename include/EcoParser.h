#ifndef ECOPARSER_H
#define ECOPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>

// Names of the 16 ECO numeric parameters (in order)
static const QStringList ECO_PARAM_NAMES = {
    "PP-SS","PL-EM","EM-V1","V1-JU","JU-R0","PM06","PM09",
    "LNHSH","R7-R8","FL-VS","TRIFL","RWDTH","RHGHT","R1PPO","OPTBI","SLOBI"
};

struct EcoRow {
    QString ecoNum;           // 6 chars   (positions 0-5)
    QString ecoName;          // 16 chars  (positions 7-22)
    QString mg;               // 2 chars
    QString tm;               // 2 chars
    QVector<double> params;   // 16 values
    bool isMinMax = false;
};

class EcoParser
{
public:
    static QVector<EcoRow> parse(const QString &filePath, QStringList &headerLines);
    static bool write(const QString &filePath,
                      const QVector<EcoRow> &rows,
                      const QStringList &headerLines);
    static QString formatRow(const EcoRow &row);
    static QString formatParam(double value, int paramIndex);
};

#endif // ECOPARSER_H
