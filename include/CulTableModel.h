#ifndef CULTABLEMODEL_H
#define CULTABLEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QStringList>
#include <optional>
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
    void addRow(const QString &vrName = "NEW CULTIVAR");
    void addRowWithData(const QString &vrName, const QString &expNo, const QString &ecoNum);
    void addRowWithFullData(const QString &vrName, const QString &expNo, const QString &ecoNum, const QVector<std::optional<double>> &params);
    void duplicateRow(int row);
    void deleteRow(int row);
    void setRowPreComment(int row, const QString &comment);

    // Column indices
    static const int COL_VARNUM  = 0;
    static const int COL_VRNAME  = 1;
    static const int COL_EXPNO   = 2;
    static const int COL_ECONUM  = 3;
    // Display shows the file's text; sort on this so parameter columns sort as numbers
    static const int SortRole = Qt::UserRole + 1;
    static const int COL_PARAM0  = 4;   // Parameters start at column 4

    // Set dynamic parameter names from the parsed CUL file
    void setParamNames(const QStringList &names);

    // Column name for a given section index
    QString columnName(int col) const;

    void setColumnTooltips(const QMap<QString, QString> &tips) { m_tips = tips; }
    void setCalibrationTypes(const QMap<QString, QString> &types);
    void setCalibrationTypeForParam(const QString &paramName, const QString &type);
    QMap<QString, QString> calibrationTypes() const { return m_calibTypes; }

    // Values a parameter may take: MINIMA–MAXIMA widened by 10% of that span on
    // each side. False when the file has no usable MINIMA/MAXIMA for it.
    bool allowedRange(int paramIdx, double *lo, double *hi) const;
    // Why `value` may not be entered for this parameter; empty if it may
    QString rangeError(int paramIdx, double value) const;

    // Tint the row with this VAR# (strength 1 = full, 0 = none); empty varNum clears
    void setHighlight(const QString &varNum, qreal strength);

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
    void valueRejected(const QString &reason);
    void calibrationTypeChanged(const QString &paramName, const QString &type);

private:
    bool isOutOfRange(int paramIdx, double value) const;
    QString generateUniqueVarNum() const;
    QVector<CulRow> m_rows;
    QVector<std::optional<double>> m_minParams;
    QVector<std::optional<double>> m_maxParams;
    QStringList m_paramNames;
    QMap<QString, QString> m_tips;
    QMap<QString, QString> m_calibTypes;  // paramName -> "P" | "G" | "N"
    const QVector<ParamFormat> &formats() const;
    mutable QVector<ParamFormat> m_formats;
    mutable bool m_formatsStale = true;
    QString m_highlightVar;
    qreal   m_highlightStrength = 0;
    void emitRowChanged(const QString &varNum);
};

#endif // CULTABLEMODEL_H
