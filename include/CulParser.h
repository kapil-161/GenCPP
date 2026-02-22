#ifndef CULPARSER_H
#define CULPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

// Names of the 18 CUL numeric parameters (in order)
static const QStringList CUL_PARAM_NAMES = {
    "CSDL","PPSEN","EM-FL","FL-SH","FL-SD","SD-PM","FL-LF",
    "LFMAX","SLAVR","SIZLF","XFRT","WTPSD","SFDUR","SDPDV",
    "PODUR","THRSH","SDPRO","SDLIP"
};

struct CulRow {
    QString varNum;           // 6 chars   (positions 0-5)
    QString vrName;           // 13 chars  (positions 7-19)
    QString expNo;            // 1 char    (position 20); '.' for MINIMA/MAXIMA
    QString ecoNum;           // 6 chars   (positions 30-35)
    QVector<double> params;   // 18 values
    bool isMinMax = false;    // true if varNum == "999991" or "999992"
};

class CulParser
{
public:
    // Parse a .CUL file. headerLines receives *, !, @ lines in order.
    static QVector<CulRow> parse(const QString &filePath, QStringList &headerLines);

    // Write rows back to filePath using fixed-width format.
    static bool write(const QString &filePath,
                      const QVector<CulRow> &rows,
                      const QStringList &headerLines);

    // Format one CUL data row as a fixed-width string.
    static QString formatRow(const CulRow &row);

    // Format one numeric parameter by index (Fortran-style widths).
    static QString formatParam(double value, int paramIndex);

    // Parse the "! COEFF  DEFINITIONS" comment block from header lines
    // into a map of  variableName -> description.
    // Works for both .CUL and .ECO header lines.
    static QMap<QString, QString> tooltipsFromHeader(const QStringList &headerLines);

    // Parse a single CUL data line (e.g. pasted from GLUE output).
    // Returns a row with varNum.isEmpty() == true on failure.
    static CulRow parseLine(const QString &line);

    // Parse the "!Calibration  P  G  N ..." line from header lines.
    // Returns map of paramName -> "P", "G", or "N".
    static QMap<QString, QString> calibrationTypes(const QStringList &headerLines);
};

#endif // CULPARSER_H
