#include "EcoTableModel.h"
#include "Config.h"
#include <QBrush>
#include <QFont>

EcoTableModel::EcoTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

void EcoTableModel::setRows(const QVector<EcoRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

int EcoTableModel::rowCount(const QModelIndex &) const { return m_rows.size(); }
int EcoTableModel::columnCount(const QModelIndex &) const { return TOTAL_COLS; }

QString EcoTableModel::columnName(int col)
{
    switch (col) {
    case COL_ECONUM:  return "ECO#";
    case COL_ECONAME: return "ECONAME";
    case COL_MG:      return "MG";
    case COL_TM:      return "TM";
    case COL_REFS:    return "CUL refs";
    default: {
        int p = col - COL_PARAM0;
        if (p >= 0 && p < ECO_PARAM_NAMES.size())
            return ECO_PARAM_NAMES[p];
    }
    }
    return QString();
}

QVariant EcoTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const EcoRow &row = m_rows[index.row()];
    int col = index.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case COL_ECONUM:  return row.ecoNum;
        case COL_ECONAME: return row.ecoName;
        case COL_MG:      return row.mg;
        case COL_TM:      return row.tm;
        case COL_REFS:    return m_refCounts.value(row.ecoNum, 0);
        default: {
            int p = col - COL_PARAM0;
            if (p >= 0 && p < row.params.size())
                return row.params[p];
        }
        }
    }

    if (role == Qt::BackgroundRole) {
        if (row.isMinMax)  return QBrush(Config::MINMAX_COLOR);
        if (col == COL_REFS && m_refCounts.value(row.ecoNum, 0) == 0)
            return QBrush(Config::WARNING_COLOR);
        return QVariant();
    }

    if (role == Qt::FontRole && row.isMinMax) {
        QFont f; f.setBold(true); return f;
    }

    if (role == Qt::ToolTipRole) {
        if (col == COL_REFS)
            return QString("Number of cultivars referencing this ecotype");
        return m_tips.value(columnName(col), columnName(col));
    }

    return QVariant();
}

QVariant EcoTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return columnName(section);
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;
    return QVariant();
}

Qt::ItemFlags EcoTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!m_rows[index.row()].isMinMax && index.column() != COL_REFS)
        f |= Qt::ItemIsEditable;
    return f;
}

bool EcoTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size())
        return false;

    EcoRow &row = m_rows[index.row()];
    if (row.isMinMax) return false;

    int col = index.column();
    switch (col) {
    case COL_ECONUM:  row.ecoNum  = value.toString().left(6); break;
    case COL_ECONAME: row.ecoName = value.toString().left(16); break;
    case COL_MG:      row.mg      = value.toString().left(2); break;
    case COL_TM:      row.tm      = value.toString().left(2); break;
    case COL_REFS:    return false;  // read-only computed column
    default: {
        int p = col - COL_PARAM0;
        if (p >= 0 && p < row.params.size()) {
            bool ok;
            double v = value.toDouble(&ok);
            if (!ok) return false;
            row.params[p] = v;
        } else return false;
    }
    }

    emit dataChanged(index, index, {role});
    emit dataModified();
    return true;
}

void EcoTableModel::addRow()
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    EcoRow r;
    r.ecoNum  = "NEWE01";
    r.ecoName = "NEW ECOTYPE";
    r.mg      = " 0";
    r.tm      = " 0";
    r.params  = QVector<double>(16, 0.0);
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void EcoTableModel::duplicateRow(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    EcoRow r  = m_rows[row];
    r.isMinMax = false;
    r.ecoNum   = r.ecoNum + "X";
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void EcoTableModel::deleteRow(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    if (m_rows[row].isMinMax) return;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit dataModified();
}
