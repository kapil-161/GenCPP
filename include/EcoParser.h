#ifndef ECOPARSER_H
#define ECOPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>

// ECO files differ by model (CROPGRO has text MG/TM columns, CERES/CSM files have
// only numbers, some have no ECONAME column), so the column layout is read from
// the file itself: names from the "@ECO#" header, and each column's right edge
// from the data rows (DSSAT reads them by fixed position, right-aligned).
struct EcoLayout {
    bool hasName = true;          // ECONAME A16 at positions 7-22
    int dataStart = 24;           // first column after A6,1X[,A16,1X]
    QStringList names;            // column names after ECO#/ECONAME
    QVector<int> ends;            // exclusive right edge of each column
    QVector<bool> numeric;        // false for text columns (e.g. MG, TM)
    QVector<int> decimals;        // typical decimals per numeric column

    int width(int col) const { return ends[col] - (col == 0 ? dataStart : ends[col - 1]); }
};

struct EcoRow {
    QString ecoNum;               // 6 chars  (positions 0-5)
    QString ecoName;              // 16 chars (positions 7-22), empty if the file has no ECONAME
    QStringList vals;             // one string per layout column, as read or as entered
    bool isMinMax = false;
    bool misaligned = false;      // row in the file did not fit the fixed-width columns
    QString preComment;           // comment lines written just before this row
    int trailingBlanks = 0;       // blank lines that followed this row
};

class EcoParser
{
public:
    // headerLines receives everything before the first data row; footerLines (optional)
    // the comment lines after the last one; layout (optional) the column layout.
    static QVector<EcoRow> parse(const QString &filePath, QStringList &headerLines,
                                 QStringList *footerLines = nullptr, EcoLayout *layout = nullptr);
    static bool write(const QString &filePath,
                      const QVector<EcoRow> &rows,
                      const QStringList &headerLines,
                      const EcoLayout &layout,
                      const QStringList &footerLines = {});
    // Fixed-width row: every value right-aligned to its column's right edge.
    static QString formatRow(const EcoRow &row, const EcoLayout &layout);
    // One column's field exactly as formatRow writes it (leading spaces included)
    static QString formatValue(const EcoRow &row, int c, const EcoLayout &layout);
};

#endif // ECOPARSER_H
