#include "CulTableModel.h"
#include "Config.h"
#include <QColor>
#include <QFont>
#include <QBrush>

CulTableModel::CulTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

void CulTableModel::setRows(const QVector<CulRow> &rows)
{
    beginResetModel();
    m_rows = rows;

    // Extract MINIMA (999991) and MAXIMA (999992) for validation
    m_minParams.clear();
    m_maxParams.clear();
    for (const auto &r : rows) {
        if (r.varNum == "999991") m_minParams = r.params.toList().toVector();
        if (r.varNum == "999992") m_maxParams = r.params.toList().toVector();
    }

    endResetModel();
}

void CulTableModel::setMinMaxRows(const CulRow *minRow, const CulRow *maxRow)
{
    m_minParams = minRow ? minRow->params.toList().toVector() : QVector<double>();
    m_maxParams = maxRow ? maxRow->params.toList().toVector() : QVector<double>();
}

int CulTableModel::rowCount(const QModelIndex &) const { return m_rows.size(); }
int CulTableModel::columnCount(const QModelIndex &) const { return TOTAL_COLS; }

QString CulTableModel::columnName(int col)
{
    switch (col) {
    case COL_VARNUM: return "VAR#";
    case COL_VRNAME: return "VRNAME";
    case COL_EXPNO:  return "EXPNO";
    case COL_ECONUM: return "ECO#";
    default:
        int p = col - COL_PARAM0;
        if (p >= 0 && p < CUL_PARAM_NAMES.size())
            return CUL_PARAM_NAMES[p];
    }
    return QString();
}

bool CulTableModel::isOutOfRange(int paramIdx, double value) const
{
    if (m_minParams.size() > paramIdx && m_maxParams.size() > paramIdx) {
        double lo = m_minParams[paramIdx];
        double hi = m_maxParams[paramIdx];
        if (hi > lo) return (value < lo || value > hi);
    }
    return false;
}

QVariant CulTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const CulRow &row = m_rows[index.row()];
    int col = index.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case COL_VARNUM: return row.varNum;
        case COL_VRNAME: return row.vrName;
        case COL_EXPNO:  return row.expNo;
        case COL_ECONUM: return row.ecoNum;
        default: {
            int p = col - COL_PARAM0;
            if (p >= 0 && p < row.params.size())
                return row.params[p];
        }
        }
    }

    if (role == Qt::BackgroundRole) {
        if (row.isMinMax)
            return QBrush(Config::MINMAX_COLOR);
        if (col >= COL_PARAM0) {
            int p = col - COL_PARAM0;
            if (p < row.params.size() && isOutOfRange(p, row.params[p]))
                return QBrush(Config::OOR_COLOR);
        }
        return QVariant();
    }

    if (role == Qt::FontRole && row.isMinMax) {
        QFont f;
        f.setBold(true);
        return f;
    }

    if (role == Qt::ToolTipRole) {
        QString name = columnName(col);
        QString tip = m_tips.value(name, name);

        // Add min/max range info if out of range
        if (col >= COL_PARAM0) {
            int p = col - COL_PARAM0;
            if (p < row.params.size() && isOutOfRange(p, row.params[p])) {
                double lo = (p < m_minParams.size()) ? m_minParams[p] : 0.0;
                double hi = (p < m_maxParams.size()) ? m_maxParams[p] : 0.0;
                tip += QString("\n\n⚠️ OUT OF RANGE");
                tip += QString("\nValue: %1").arg(row.params[p]);
                tip += QString("\nAllowed: %1 to %2").arg(lo).arg(hi);
            }
        }

        return tip;
    }

    return QVariant();
}

QVariant CulTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal) {
        if (role == Qt::DisplayRole) return section + 1;
        return QVariant();
    }

    const QString name = columnName(section);

    if (role == Qt::DisplayRole) {
        if (section >= COL_PARAM0) {
            const QString cal = m_calibTypes.value(name);
            if (!cal.isEmpty())
                return name + "\n[" + cal + "]";
        }
        return name;
    }

    if (role == Qt::ForegroundRole && section >= COL_PARAM0) {
        const QString cal = m_calibTypes.value(name);
        if (cal == "P") return QColor("#1565C0");   // blue  — Phenology
        if (cal == "G") return QColor("#2E7D32");   // green — Growth
        if (cal == "N") return QColor("#9E9E9E");   // grey  — Not calibrated
    }

    if (role == Qt::ToolTipRole) {
        QString tip = m_tips.value(name, name);
        const QString cal = m_calibTypes.value(name);
        if (cal == "P") tip += "\n\nCalibration: Phenology";
        else if (cal == "G") tip += "\n\nCalibration: Growth";
        else if (cal == "N") tip += "\n\nCalibration: Not used";
        return tip;
    }

    if (role == Qt::FontRole) {
        QFont f;
        f.setBold(true);
        return f;
    }

    return QVariant();
}

Qt::ItemFlags CulTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    // MINIMA/MAXIMA rows are read-only
    if (!m_rows[index.row()].isMinMax)
        f |= Qt::ItemIsEditable;
    return f;
}

bool CulTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size())
        return false;

    CulRow &row = m_rows[index.row()];
    if (row.isMinMax) return false;

    int col = index.column();
    switch (col) {
    case COL_VARNUM: row.varNum = value.toString().left(6); break;
    case COL_VRNAME: row.vrName = value.toString().left(13); break;
    case COL_EXPNO:  row.expNo  = value.toString().left(1); break;
    case COL_ECONUM: row.ecoNum = value.toString().left(6); break;
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

void CulTableModel::addRow()
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    CulRow r;
    r.varNum  = "NEW001";
    r.vrName  = "NEW CULTIVAR";
    r.expNo   = " ";
    r.ecoNum  = "DFAULT";
    r.params  = QVector<double>(18, 0.0);
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void CulTableModel::duplicateRow(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    CulRow r = m_rows[row];
    r.isMinMax = false;
    r.varNum   = r.varNum + "X";   // User should rename
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void CulTableModel::deleteRow(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    if (m_rows[row].isMinMax) return;  // Protect MINIMA/MAXIMA
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit dataModified();
}

QString CulTableModel::Violation::toString() const
{
    return QString("%1: %2=%3 (range: %4 to %5)")
        .arg(varNum)
        .arg(paramName)
        .arg(value)
        .arg(minVal)
        .arg(maxVal);
}

QVector<CulTableModel::Violation> CulTableModel::getViolations() const
{
    QVector<Violation> violations;

    for (int r = 0; r < m_rows.size(); ++r) {
        const CulRow &row = m_rows[r];
        if (row.isMinMax) continue;  // Skip MINIMA/MAXIMA rows

        for (int p = 0; p < row.params.size() && p < CUL_PARAM_NAMES.size(); ++p) {
            if (isOutOfRange(p, row.params[p])) {
                Violation v;
                v.row       = r;
                v.varNum    = row.varNum;
                v.paramName = CUL_PARAM_NAMES[p];
                v.value     = row.params[p];
                v.minVal    = (p < m_minParams.size()) ? m_minParams[p] : 0.0;
                v.maxVal    = (p < m_maxParams.size()) ? m_maxParams[p] : 0.0;
                violations.append(v);
            }
        }
    }

    return violations;
}
