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
        if (r.varNum == "999991") m_minParams = r.params;
        if (r.varNum == "999992") m_maxParams = r.params;
    }

    endResetModel();
}

void CulTableModel::setMinMaxRows(const CulRow *minRow, const CulRow *maxRow)
{
    m_minParams = minRow ? minRow->params : QVector<std::optional<double>>();
    m_maxParams = maxRow ? maxRow->params : QVector<std::optional<double>>();
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
        if (m_minParams[paramIdx].has_value() && m_maxParams[paramIdx].has_value()) {
            double lo = m_minParams[paramIdx].value();
            double hi = m_maxParams[paramIdx].value();
            if (hi > lo) return (value < lo || value > hi);
        }
    }
    return false;
}

QVariant CulTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const CulRow &row = m_rows[index.row()];
    int col = index.column();

    if (role == Qt::DisplayRole) {
        switch (col) {
        case COL_VARNUM: return row.varNum;
        case COL_VRNAME: return row.vrName;
        case COL_EXPNO:  return row.expNo;
        case COL_ECONUM: return row.ecoNum;
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
        case COL_VARNUM: return row.varNum;
        case COL_VRNAME: return row.vrName;
        case COL_EXPNO:  return row.expNo;
        case COL_ECONUM: return row.ecoNum;
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
        if (row.isMinMax)
            return QBrush(Config::MINMAX_COLOR);
        if (col >= COL_PARAM0) {
            int p = col - COL_PARAM0;
            if (p < row.params.size() && row.params[p].has_value() && isOutOfRange(p, row.params[p].value()))
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
            if (p < row.params.size() && row.params[p].has_value() && isOutOfRange(p, row.params[p].value())) {
                double lo = (p < m_minParams.size() && m_minParams[p].has_value()) ? m_minParams[p].value() : 0.0;
                double hi = (p < m_maxParams.size() && m_maxParams[p].has_value()) ? m_maxParams[p].value() : 0.0;
                tip += QString("\n\n⚠️ OUT OF RANGE");
                tip += QString("\nValue: %1").arg(row.params[p].value());
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

void CulTableModel::addRow(const QString &vrName)
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    CulRow r;
    r.varNum  = generateUniqueVarNum();
    r.vrName  = vrName;
    r.expNo   = " ";
    r.ecoNum  = "DFAULT";
    r.params  = QVector<std::optional<double>>(18);  // Initialize with nullopt
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void CulTableModel::addRowWithData(const QString &vrName, const QString &expNo, const QString &ecoNum)
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    CulRow r;
    r.varNum  = generateUniqueVarNum();
    r.vrName  = vrName;
    r.expNo   = expNo;
    r.ecoNum  = ecoNum;
    r.params  = QVector<std::optional<double>>(18);  // Initialize with nullopt
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void CulTableModel::addRowWithFullData(const QString &vrName, const QString &expNo, const QString &ecoNum, const QVector<std::optional<double>> &params)
{
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    CulRow r;
    r.varNum  = generateUniqueVarNum();
    r.vrName  = vrName;
    r.expNo   = expNo;
    r.ecoNum  = ecoNum;
    
    // Copy optional values directly
    r.params = QVector<std::optional<double>>(18);
    for (int i = 0; i < params.size() && i < 18; ++i) {
        r.params[i] = params[i];
    }
    
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

QString CulTableModel::generateUniqueVarNum() const
{
    // Find the most recent crop code (first 2 chars) and highest number for that code
    QString cropCode = "NEW";  // Default fallback
    int maxNum = 0;
    
    for (const auto &row : m_rows) {
        if (row.varNum.length() >= 6) {
            QString code = row.varNum.left(2);
            bool ok;
            int num = row.varNum.right(4).toInt(&ok);
            if (ok) {
                // Use the last non-MINMAX code we find, and track the highest number
                if (!row.isMinMax && code != "99") {
                    cropCode = code;
                    if (num > maxNum)
                        maxNum = num;
                }
            }
        }
    }
    
    // Format as [CROP_CODE] + 4-digit zero-padded number
    return QString("%1%2").arg(cropCode).arg(maxNum + 1, 4, 10, QChar('0'));
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
            if (row.params[p].has_value() && isOutOfRange(p, row.params[p].value())) {
                Violation v;
                v.row       = r;
                v.varNum    = row.varNum;
                v.paramName = CUL_PARAM_NAMES[p];
                v.value     = row.params[p].has_value() ? row.params[p].value() : 0.0;
                v.minVal    = (p < m_minParams.size() && m_minParams[p].has_value()) ? m_minParams[p].value() : 0.0;
                v.maxVal    = (p < m_maxParams.size() && m_maxParams[p].has_value()) ? m_maxParams[p].value() : 0.0;
                violations.append(v);
            }
        }
    }

    return violations;
}
