#ifndef ECOTABLEMODEL_H
#define ECOTABLEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QMap>
#include "EcoParser.h"

class EcoTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit EcoTableModel(QObject *parent = nullptr);

    void setRows(const QVector<EcoRow> &rows);
    QVector<EcoRow> rows() const { return m_rows; }

    // Count how many CUL rows reference each ECO#
    void setCulCrossRef(const QMap<QString, int> &refCounts) { m_refCounts = refCounts; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    void addRow();
    void duplicateRow(int row);
    void deleteRow(int row);

    static const int COL_ECONUM  = 0;
    static const int COL_ECONAME = 1;
    static const int COL_MG      = 2;
    static const int COL_TM      = 3;
    static const int COL_REFS    = 4;   // # of cultivars using this ECO
    static const int COL_PARAM0  = 5;
    static const int TOTAL_COLS  = 21;  // 5 fixed + 16 params

    static QString columnName(int col);
    void setColumnTooltips(const QMap<QString, QString> &tips) { m_tips = tips; }

signals:
    void dataModified();

private:
    QVector<EcoRow> m_rows;
    QMap<QString, int> m_refCounts;
    QMap<QString, QString> m_tips;
};

#endif // ECOTABLEMODEL_H
