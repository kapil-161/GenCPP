#include "FrozenTableView.h"
#include "CalibHeaderView.h"
#include <QHeaderView>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>

FrozenTableView::FrozenTableView(int frozenColumns, bool calibrationBand, QWidget *parent)
    : QTableView(parent)
    , m_frozen(new QTableView(this))
    , m_count(frozenColumns)
{
    // Before any connection below is made to the headers
    if (calibrationBand) {
        for (QTableView *v : {static_cast<QTableView *>(this), m_frozen}) {
            auto *h = new CalibHeaderView(v);
            v->setHorizontalHeader(h);
            connect(h, &CalibHeaderView::calibrationClicked, this, &FrozenTableView::calibrationClicked);
        }
        m_frozen->raise();   // the new main header was stacked above the frozen overlay
    }

    m_frozen->setFocusPolicy(Qt::NoFocus);
    m_frozen->verticalHeader()->hide();
    m_frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollMode(ScrollPerPixel);
    m_frozen->setFrameShape(QFrame::NoFrame);
    m_frozen->setStyleSheet("QTableView { border: none; border-right: 2px solid #C9D6DF; }");
    m_frozen->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_frozen->setSortingEnabled(true);          // header clicks sort the shared proxy
    // A third click on a column name goes back to file order
    horizontalHeader()->setSortIndicatorClearable(true);
    m_frozen->horizontalHeader()->setSortIndicatorClearable(true);
    viewport()->stackUnder(m_frozen);

    setHorizontalScrollMode(ScrollPerPixel);
    setVerticalScrollMode(ScrollPerPixel);

    // Keep both views scrolled together and the frozen widths equal to ours
    connect(m_frozen->verticalScrollBar(), &QAbstractSlider::valueChanged,
            verticalScrollBar(), &QAbstractSlider::setValue);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged,
            m_frozen->verticalScrollBar(), &QAbstractSlider::setValue);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int col, int, int size) {
                if (col < m_count) { m_frozen->setColumnWidth(col, size); updateFrozenGeometry(); }
            });
    connect(verticalHeader(), &QHeaderView::sectionResized, this,
            [this](int row, int, int size) { m_frozen->setRowHeight(row, size); });
    connect(horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            m_frozen->horizontalHeader(), &QHeaderView::setSortIndicator);
}

void FrozenTableView::setModel(QAbstractItemModel *model)
{
    QTableView::setModel(model);
    m_frozen->setModel(model);
    m_frozen->setSelectionModel(selectionModel());
    m_frozen->setItemDelegate(itemDelegate());
    m_frozen->setEditTriggers(editTriggers());
    if (model) {
        // Parameter columns differ per crop: re-hide after every structural change
        connect(model, &QAbstractItemModel::modelReset, this, &FrozenTableView::syncColumns,
                Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::layoutChanged, this, &FrozenTableView::syncColumns,
                Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::columnsInserted, this, &FrozenTableView::syncColumns,
                Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::headerDataChanged, this, &FrozenTableView::syncColumns,
                Qt::UniqueConnection);
    }
    syncColumns();
}

void FrozenTableView::setSelectionModel(QItemSelectionModel *selectionModel)
{
    QTableView::setSelectionModel(selectionModel);
    m_frozen->setSelectionModel(selectionModel);
}

void FrozenTableView::setColumnDelegate(int column, QAbstractItemDelegate *delegate)
{
    setItemDelegateForColumn(column, delegate);
    m_frozen->setItemDelegateForColumn(column, delegate);
}

void FrozenTableView::enableUserColumnWidths(const QString &tableName)
{
    m_widthTable = tableName;
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_frozen->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

    // The frozen columns' edges are on the overlay: pass its drags to our columns
    connect(m_frozen->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int col, int, int size) {
                if (col < m_count && columnWidth(col) != size) setColumnWidth(col, size);
            });
    // Remember what the user sets (not our own fitting)
    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int col, int, int size) {
                if (!m_applyingWidths && !widthKey(col).isEmpty())
                    QSettings("DSSAT", "GeneticsEditor").setValue(widthKey(col), size);
            });
    if (model()) {
        // After the view has laid out the new rows
        connect(model(), &QAbstractItemModel::modelReset, this,
                [this]() { QTimer::singleShot(0, this, &FrozenTableView::applyColumnWidths); });
        QTimer::singleShot(0, this, &FrozenTableView::applyColumnWidths);
    }
}

QString FrozenTableView::widthKey(int column) const
{
    if (m_widthTable.isEmpty() || !model()) return {};
    const QString name = model()->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
    if (name.isEmpty()) return {};
    return QString("ColumnWidths/%1/%2/%3").arg(m_widthTable, m_widthScope.isEmpty() ? "_" : m_widthScope, name);
}

void FrozenTableView::applyColumnWidths()
{
    if (!model()) return;
    m_applyingWidths = true;
    resizeColumnsToContents();
    QSettings s("DSSAT", "GeneticsEditor");
    for (int c = 0; c < model()->columnCount(); ++c) {
        const QString key = widthKey(c);
        const int w = key.isEmpty() ? 0 : s.value(key, 0).toInt();
        if (w > 0) setColumnWidth(c, w);
    }
    m_applyingWidths = false;
    syncColumns();
}

void FrozenTableView::syncColumns()
{
    if (!model()) return;
    m_frozen->setAlternatingRowColors(alternatingRowColors());
    m_frozen->setSelectionBehavior(selectionBehavior());
    m_frozen->setSelectionMode(selectionMode());
    for (int c = 0; c < model()->columnCount(); ++c) {
        m_frozen->setColumnHidden(c, c >= m_count);
        if (c < m_count) m_frozen->setColumnWidth(c, columnWidth(c));
    }
    for (int r = 0; r < model()->rowCount(); ++r)
        m_frozen->setRowHeight(r, rowHeight(r));
    updateFrozenGeometry();
}

int FrozenTableView::frozenWidth() const
{
    int w = 0;
    for (int c = 0; c < m_count && model() && c < model()->columnCount(); ++c)
        w += columnWidth(c);
    return w;
}

void FrozenTableView::updateFrozenGeometry()
{
    // Same row height as ours (set on our header after construction), or rows drift apart
    QHeaderView *ours = verticalHeader(), *theirs = m_frozen->verticalHeader();
    if (theirs->minimumSectionSize() != ours->minimumSectionSize())
        theirs->setMinimumSectionSize(ours->minimumSectionSize());
    if (theirs->defaultSectionSize() != ours->defaultSectionSize())
        theirs->setDefaultSectionSize(ours->defaultSectionSize());
    m_frozen->horizontalHeader()->setFixedHeight(horizontalHeader()->height());
    m_frozen->setGeometry(verticalHeader()->width() + frameWidth(), frameWidth(),
                          frozenWidth() + 2, viewport()->height() + horizontalHeader()->height());
}

void FrozenTableView::updateGeometries()
{
    QTableView::updateGeometries();
    if (m_frozen) updateFrozenGeometry();
}

void FrozenTableView::resizeEvent(QResizeEvent *event)
{
    QTableView::resizeEvent(event);
    updateFrozenGeometry();
}

// Keyboard moves into a scrolled-under column: scroll so the cell clears the frozen part
QModelIndex FrozenTableView::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    QModelIndex current = QTableView::moveCursor(cursorAction, modifiers);
    if (cursorAction == MoveLeft && current.column() >= m_count
        && visualRect(current).topLeft().x() < frozenWidth()) {
        const int newValue = horizontalScrollBar()->value() + visualRect(current).topLeft().x() - frozenWidth();
        horizontalScrollBar()->setValue(newValue);
    }
    return current;
}

void FrozenTableView::scrollTo(const QModelIndex &index, ScrollHint hint)
{
    if (index.column() >= m_count) {
        QTableView::scrollTo(index, hint);
    } else if (index.isValid()) {   // frozen columns are always visible: only scroll rows
        const int h = horizontalScrollBar()->value();
        QTableView::scrollTo(index, hint);
        horizontalScrollBar()->setValue(h);
    }
}
