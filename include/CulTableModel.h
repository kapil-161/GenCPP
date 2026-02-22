#ifndef CULTABLEMODEL_H
#define CULTABLEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QStringList>
#include "CulParser.h"

class CulTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit CulTableModel(QObject *parent = nullptr);

    // Load/store
    void setRows(const QVector<CulRow> &rows);
    QVector<CulRow> rows() const { return m_rows; }

    // MINIMA/MAXIMA rows for range validation
    void setMinMaxRows(const CulRow *minRow, const CulRow *maxRow);

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    // Mutation helpers
    void addRow();
    void duplicateRow(int row);
    void deleteRow(int row);

    // Column indices
    static const int COL_VARNUM  = 0;
    static const int COL_VRNAME  = 1;
    static const int COL_EXPNO   = 2;
    static const int COL_ECONUM  = 3;
    static const int COL_PARAM0  = 4;   // CSDL is column 4

    // Total columns = 4 fixed + 18 params
    static const int TOTAL_COLS  = 22;

    // Column name for a given section index
    static QString columnName(int col);

    void setColumnTooltips(const QMap<QString, QString> &tips) { m_tips = tips; }
    void setCalibrationTypes(const QMap<QString, QString> &types) { m_calibTypes = types; emit headerDataChanged(Qt::Horizontal, COL_PARAM0, TOTAL_COLS - 1); }

    // Validation: returns a list of violations (e.g., "CAND01: PPSEN=0.5 (range: -0.2 to -0.04)")
    struct Violation {
        int row;
        QString varNum;
        QString paramName;
        double value;
        double minVal;
        double maxVal;
        QString toString() const;
    };
    QVector<Violation> getViolations() const;

signals:
    void dataModified();

private:
    bool isOutOfRange(int paramIdx, double value) const;
    QVector<CulRow> m_rows;
    QVector<double> m_minParams;
    QVector<double> m_maxParams;
    QMap<QString, QString> m_tips;
    QMap<QString, QString> m_calibTypes;  // paramName -> "P" | "G" | "N"
};

#endif // CULTABLEMODEL_H
