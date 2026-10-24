#ifndef CULPARSER_H
#define CULPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <optional>

// Names of the 18 CUL numeric parameters (used as fallback)
static const QStringList CUL_PARAM_NAMES = {
    "CSDL","PPSEN","EM-FL","FL-SH","FL-SD","SD-PM","FL-LF",
    "LFMAX","SLAVR","SIZLF","XFRT","WTPSD","SFDUR","SDPDV",
    "PODUR","THRSH","SDPRO","SDLIP"
};

struct ParamFormat {
    int width = 6;
    int decimals = 1;
    bool trailingDot = false;
};

struct CulRow {
    QString varNum;                              // 6 chars   (positions 0-5)
    QString vrName;                              // 16 chars  (positions 7-22)
    QString expNo;                               // 7 chars   (positions 23-29); e.g. "   1,6 " or "      ."
    QString ecoNum;                              // 6 chars   (positions 30-35)
    QVector<std::optional<double>> params;       // dynamic values
    QVector<QString> paramStrs;                  // Original unparsed strings for exact reproduction
    bool isMinMax = false;                       // true if varNum == "999991" or "999992"
    int fWidth = 6;                              // Inferred fixed-width format (e.g. 6 or 15)
    int trailingBlanks = 0;                      // number of blank lines that followed this row in the source file
    bool misaligned = false;                     // row in the file did not fit the fixed-width columns
    QString preComment;                          // inline history comment written before this row
};

class CulParser
{
public:
    // Parse a .CUL file. headerLines receives *, !, @ lines in order.
    // footerLines (optional) receives ! comment lines after the last data row.
    static QVector<CulRow> parse(const QString &filePath, QStringList &headerLines,
                                 QStringList *footerLines = nullptr);

    // Write rows back to filePath using fixed-width format and inferred decimal counts.
    static bool write(const QString &filePath,
                      const QVector<CulRow> &rows,
                      const QStringList &headerLines,
                      const QStringList &paramNames,
                      const QStringList &footerLines = {});

    // Write lines (Latin1) using the line endings of the file being replaced.
    static bool writeLines(const QString &filePath, const QStringList &lines);

    // Format one CUL data row as a fixed-width string using pre-inferred formats.
    static QString formatRow(const CulRow &row, const QVector<ParamFormat> &formats, int numParams);

    // Format one numeric parameter given its specific format rules.
    static QString formatParam(double value, const ParamFormat &fmt);

    // Infer column formatter (width, decimal layout) dynamically from file content
    static QVector<ParamFormat> inferFormats(const QVector<CulRow> &rows, int numParams);

    // Extract sequence of dynamic parameter names from the @VAR# line
    static QStringList extractParamNames(const QStringList &headerLines);

    // Parse the "! COEFF  DEFINITIONS" comment block from header lines
    // into a map of  variableName -> description.
    // Works for both .CUL and .ECO header lines.
    static QMap<QString, QString> tooltipsFromHeader(const QStringList &headerLines);

    // Return a 6-char ID not in `existing`, keeping srcId's letter prefix and
    // incrementing its number (IB0488 -> IB1501 if IB1500 is the highest IB id).
    static QString nextFreeId(const QString &srcId, const QStringList &existing);

    // Parse a single CUL data line (e.g. pasted from GLUE output).
    // Returns a row with varNum.isEmpty() == true on failure.
    static CulRow parseLine(const QString &line);

    // Parse the "!Calibration  P  G  N ..." line from header lines.
    // Returns map of paramName -> "P", "G", or "N".
    static QMap<QString, QString> calibrationTypes(const QStringList &headerLines);
};

#endif // CULPARSER_H
