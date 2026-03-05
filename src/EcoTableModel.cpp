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

    if (role == Qt::DisplayRole) {
        switch (col) {
        case COL_ECONUM:  return row.ecoNum;
        case COL_ECONAME: return row.ecoName;
        case COL_MG:      return row.mg;
        case COL_TM:      return row.tm;
        case COL_REFS:    return m_refCounts.value(row.ecoNum, 0);
        default: {
            int p = col - COL_PARAM0;
            if (p >= 0 && p < row.params.size()) {
                // Show empty if no value (std::nullopt), show value if set (including 0)
                if (row.params[p].has_value())
                    return row.params[p].value();
                return QString();
            }
        }
        }
    }
    
    if (role == Qt::EditRole) {
        switch (col) {
        case COL_ECONUM:  return row.ecoNum;
        case COL_ECONAME: return row.ecoName;
        case COL_MG:      return row.mg;
        case COL_TM:      return row.tm;
        case COL_REFS:    return m_refCounts.value(row.ecoNum, 0);
        default: {
            int p = col - COL_PARAM0;
            if (p >= 0 && p < row.params.size()) {
                if (row.params[p].has_value())
                    return row.params[p].value();
                return 0.0;
            }
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
            QString str = value.toString().trimmed();
            if (str.isEmpty()) {
                row.params[p] = std::nullopt;  // Empty = no value
            } else {
                bool ok;
                double v = str.toDouble(&ok);
                if (!ok) return false;
                row.params[p] = std::optional<double>(v);
            }
        } else return false;
    }
    }

    emit dataChanged(index, index, {role});
    emit dataModified();
    return true;
}

void EcoTableModel::addRow(const QString &ecoName)
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    EcoRow r;
    r.ecoNum  = generateUniqueEcoNum();
    r.ecoName = ecoName;
    r.mg      = " 0";
    r.tm      = " 0";
    r.params  = QVector<std::optional<double>>(16);  // Initialize with nullopt
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void EcoTableModel::addRowWithData(const QString &ecoName, const QString &mg, const QString &tm)
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    EcoRow r;
    r.ecoNum  = generateUniqueEcoNum();
    r.ecoName = ecoName;
    r.mg      = mg;
    r.tm      = tm;
    r.params  = QVector<std::optional<double>>(16);  // Initialize with nullopt
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void EcoTableModel::addRowWithFullData(const QString &ecoName, const QString &mg, const QString &tm, const QVector<std::optional<double>> &params)
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    EcoRow r;
    r.ecoNum  = generateUniqueEcoNum();
    r.ecoName = ecoName;
    r.mg      = mg;
    r.tm      = tm;
    
    // Copy optional values directly
    r.params = QVector<std::optional<double>>(16);
    for (int i = 0; i < params.size() && i < 16; ++i) {
        r.params[i] = params[i];
    }
    
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

QString EcoTableModel::generateUniqueEcoNum() const
{
    // Find the prefix (e.g., "G", "D") and highest number
    QString prefix = "ECO";  // Default fallback
    int maxNum = 0;
    
    for (const auto &row : m_rows) {
        if (row.ecoNum.length() >= 5) {
            QString code = row.ecoNum.left(1);
            bool ok;
            int num = row.ecoNum.right(4).toInt(&ok);
            if (ok && !row.isMinMax) {
                prefix = code;
                if (num > maxNum)
                    maxNum = num;
            }
        }
    }
    
    // Format as [PREFIX] + 4-digit zero-padded number
    return QString("%1%2").arg(prefix).arg(maxNum + 1, 4, 10, QChar('0'));
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
