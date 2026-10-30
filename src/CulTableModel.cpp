#include "CulTableModel.h"
#include "Config.h"
#include <QColor>
#include <QFont>
#include <QBrush>

CulTableModel::CulTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_paramNames(CUL_PARAM_NAMES)
{
    // Column formats are inferred from all rows, like the writer does, so any
    // change to the data can change how other cells in that column are written.
    auto stale = [this]() { m_formatsStale = true; };
    connect(this, &QAbstractItemModel::modelReset,   this, stale);
    connect(this, &QAbstractItemModel::rowsInserted, this, stale);
    connect(this, &QAbstractItemModel::rowsRemoved,  this, stale);
    connect(this, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (roles.isEmpty() || roles.contains(Qt::DisplayRole) || roles.contains(Qt::EditRole))
                    m_formatsStale = true;
            });
}

const QVector<ParamFormat> &CulTableModel::formats() const
{
    if (m_formatsStale) {
        m_formats = CulParser::inferFormats(m_rows, m_paramNames.size());
        m_formatsStale = false;
    }
    return m_formats;
}

void CulTableModel::setParamNames(const QStringList &names)
{
    beginResetModel();
    m_paramNames = names;
    if (m_paramNames.isEmpty()) m_paramNames = CUL_PARAM_NAMES; // fallback
    endResetModel();
}

void CulTableModel::setCalibrationTypes(const QMap<QString, QString> &types)
{
    m_calibTypes = types;
    emit headerDataChanged(Qt::Horizontal, COL_PARAM0, columnCount() - 1);
    if (rowCount() > 0)   // [N] columns' values are drawn grey
        emit dataChanged(index(0, COL_PARAM0), index(rowCount() - 1, columnCount() - 1),
                         {Qt::ForegroundRole});
}

void CulTableModel::setCalibrationTypeForParam(const QString &paramName, const QString &type)
{
    if (type.isEmpty())
        m_calibTypes.remove(paramName);
    else
        m_calibTypes[paramName] = type;

    for (int col = COL_PARAM0; col < columnCount(); ++col) {
        if (columnName(col) == paramName) {
            emit headerDataChanged(Qt::Horizontal, col, col);
            if (rowCount() > 0)
                emit dataChanged(index(0, col), index(rowCount() - 1, col), {Qt::ForegroundRole});
            break;
        }
    }
    emit calibrationTypeChanged(paramName, type);
}

void CulTableModel::setRows(const QVector<CulRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    m_highlightVar.clear();
    m_highlightStrength = 0;

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
int CulTableModel::columnCount(const QModelIndex &) const { return COL_PARAM0 + m_paramNames.size(); }

QString CulTableModel::columnName(int col) const
{
    switch (col) {
    case COL_VARNUM: return "VAR#";
    case COL_VRNAME: return "VRNAME";
    case COL_EXPNO:  return "EXPNO";
    case COL_ECONUM: return "ECO#";
    default:
        int p = col - COL_PARAM0;
        if (p >= 0 && p < m_paramNames.size())
            return m_paramNames[p];
    }
    return QString();
}

bool CulTableModel::allowedRange(int paramIdx, double *lo, double *hi) const
{
    if (paramIdx < 0 || paramIdx >= m_minParams.size() || paramIdx >= m_maxParams.size()) return false;
    if (!m_minParams[paramIdx].has_value() || !m_maxParams[paramIdx].has_value()) return false;
    const double mn = m_minParams[paramIdx].value(), mx = m_maxParams[paramIdx].value();
    if (mx < mn) return false;   // malformed MINIMA/MAXIMA: don't block anything
    const double margin = 0.1 * (mx - mn);
    *lo = mn - margin;
    *hi = mx + margin;
    return true;
}

QString CulTableModel::rangeError(int paramIdx, double value) const
{
    if (value == -99.0) return {};   // DSSAT missing value
    double lo, hi;
    if (!allowedRange(paramIdx, &lo, &hi) || (value >= lo && value <= hi)) return {};
    const QString name = paramIdx < m_paramNames.size() ? m_paramNames[paramIdx] : QString();
    return QString("%1 = %2 is outside the allowed range %3 to %4 "
                   "(MINIMA–MAXIMA plus 10% of that range on each side).")
        .arg(name).arg(value).arg(lo, 0, 'g', 6).arg(hi, 0, 'g', 6);
}

bool CulTableModel::isOutOfRange(int paramIdx, double value) const
{
    if (value == -99.0) return false;   // DSSAT missing value, not a range violation
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

    // Numbers right-justified, as in the fixed-width file; IDs and names left
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(Qt::AlignVCenter | (col >= COL_PARAM0 ? Qt::AlignRight : Qt::AlignLeft));

    if (role == Qt::DisplayRole) {
        switch (col) {
        case COL_VARNUM: return row.varNum;
        case COL_VRNAME: return row.vrName;
        case COL_EXPNO:  return row.expNo;
        case COL_ECONUM: return row.ecoNum;
        default: {
            // Exactly the text the file gets ("12.50", "240.", ".115", "-99")
            int p = col - COL_PARAM0;
            if (p >= 0 && p < row.params.size())
                return CulParser::formatValue(row, p, formats()).trimmed();
        }
        }
    }

    if (role == SortRole) {
        int p = col - COL_PARAM0;
        if (p < 0) return data(index, Qt::DisplayRole);
        if (p < row.params.size() && row.params[p].has_value())
            return row.params[p].value();
        return -99.0;
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
        if (m_highlightStrength > 0 && row.varNum == m_highlightVar) {
            const QColor c = Config::NEW_ROW_COLOR;
            auto mix = [this](int v) { return int(255 + (v - 255) * m_highlightStrength); };
            return QBrush(QColor(mix(c.red()), mix(c.green()), mix(c.blue())));
        }
        if (col >= COL_PARAM0) {
            int p = col - COL_PARAM0;
            if (p < row.params.size() && row.params[p].has_value() && isOutOfRange(p, row.params[p].value()))
                return QBrush(Config::OOR_COLOR);
        }
        return QVariant();
    }

    // [N] parameters are left alone by GLUE: grey their values like their header
    if (role == Qt::ForegroundRole && col >= COL_PARAM0
        && m_calibTypes.value(columnName(col)) == "N")
        return QColor("#6B7178");

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

        return Config::wrappedTip(tip);
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
        return Config::wrappedTip(tip);
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
    case COL_VARNUM: {
        // A6 cultivar ID: exactly 6 chars, no spaces, not used by another row
        const QString id = value.toString().trimmed().toUpper();
        if (id.size() != 6 || id.contains(' ')) return false;
        for (int i = 0; i < m_rows.size(); ++i)
            if (i != index.row() && m_rows[i].varNum.compare(id, Qt::CaseInsensitive) == 0) return false;
        row.varNum = id;
        break;
    }
    case COL_VRNAME: row.vrName = value.toString().trimmed().left(16); break;   // A16
    case COL_EXPNO:  row.expNo  = value.toString().left(7).leftJustified(7, ' '); break;
    case COL_ECONUM: {
        const QString eco = value.toString().trimmed().toUpper();   // A6
        if (eco.isEmpty() || eco.size() > 6 || eco.contains(' ')) return false;
        row.ecoNum = eco;
        break;
    }
    default: {
        int p = col - COL_PARAM0;
        if (p >= 0 && p < m_paramNames.size()) {
            while (row.params.size() <= p) row.params.append(std::nullopt);
            while (row.paramStrs.size() <= p) row.paramStrs.append("");

            QString str = value.toString().trimmed();
            if (str.isEmpty()) {
                row.params[p] = std::nullopt;  // Empty = no value
                row.paramStrs[p] = ""; 
            } else {
                bool ok;
                double v = str.toDouble(&ok);
                if (!ok) return false;
                const QString err = rangeError(p, v);
                if (!err.isEmpty()) {
                    emit valueRejected(err);
                    return false;
                }
                row.params[p] = std::optional<double>(v);
                row.paramStrs[p] = str; // Preserve string so decimal precision is kept on write
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
    r.expNo   = "       "; // 7-char region, blank for new rows
    r.ecoNum  = "DFAULT";
    r.params  = QVector<std::optional<double>>(m_paramNames.size());
    r.paramStrs = QVector<QString>(m_paramNames.size(), "");
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
    r.params  = QVector<std::optional<double>>(m_paramNames.size());
    r.paramStrs = QVector<QString>(m_paramNames.size(), "");
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
    r.params = QVector<std::optional<double>>(m_paramNames.size());
    r.paramStrs = QVector<QString>(m_paramNames.size(), "");
    for (int i = 0; i < params.size() && i < m_paramNames.size(); ++i) {
        r.params[i] = params[i];
    }
    
    r.isMinMax = false;
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

QString CulTableModel::generateUniqueVarNum() const
{
    // Same prefix as the last regular cultivar, next free number, always 6 chars
    QStringList ids;
    QString last;
    for (const auto &row : m_rows) {
        ids << row.varNum;
        if (!row.isMinMax) last = row.varNum;
    }
    return CulParser::nextFreeId(last.isEmpty() ? QString("XX0000") : last, ids);
}

void CulTableModel::duplicateRow(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    int n = m_rows.size();
    beginInsertRows(QModelIndex(), n, n);
    CulRow r = m_rows[row];
    r.isMinMax = false;
    r.preComment.clear();      // history comments belong to the original row
    r.trailingBlanks = 0;
    QStringList ids, names;
    for (const auto &x : m_rows) { ids << x.varNum; names << x.vrName; }
    r.varNum   = CulParser::nextFreeId(r.varNum, ids);   // must stay 6 chars (A6)
    r.vrName   = CulParser::copyName(r.vrName, names);
    m_rows.append(r);
    endInsertRows();
    emit dataModified();
}

void CulTableModel::setHighlight(const QString &varNum, qreal strength)
{
    const QString previous = m_highlightVar;
    m_highlightVar = varNum;
    m_highlightStrength = varNum.isEmpty() ? 0 : strength;
    if (!previous.isEmpty() && previous != varNum)
        emitRowChanged(previous);
    if (!varNum.isEmpty())
        emitRowChanged(varNum);
}

void CulTableModel::emitRowChanged(const QString &varNum)
{
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_rows[r].varNum == varNum) {
            emit dataChanged(index(r, 0), index(r, columnCount() - 1), {Qt::BackgroundRole});
            return;
        }
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

void CulTableModel::setRowPreComment(int row, const QString &comment)
{
    if (row < 0 || row >= m_rows.size()) return;
    m_rows[row].preComment = comment;
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

        for (int p = 0; p < row.params.size() && p < m_paramNames.size(); ++p) {
            if (row.params[p].has_value() && isOutOfRange(p, row.params[p].value())) {
                Violation v;
                v.row       = r;
                v.varNum    = row.varNum;
                v.paramName = m_paramNames[p];
                v.value     = row.params[p].has_value() ? row.params[p].value() : 0.0;
                v.minVal    = (p < m_minParams.size() && m_minParams[p].has_value()) ? m_minParams[p].value() : 0.0;
                v.maxVal    = (p < m_maxParams.size() && m_maxParams[p].has_value()) ? m_maxParams[p].value() : 0.0;
                violations.append(v);
            }
        }
    }

    return violations;
}
