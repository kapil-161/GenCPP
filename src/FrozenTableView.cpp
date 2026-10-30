#include "FrozenTableView.h"
#include <QHeaderView>
#include <QScrollBar>

FrozenTableView::FrozenTableView(int frozenColumns, QWidget *parent)
    : QTableView(parent)
    , m_frozen(new QTableView(this))
    , m_count(frozenColumns)
{
    m_frozen->setFocusPolicy(Qt::NoFocus);
    m_frozen->verticalHeader()->hide();
    m_frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollMode(ScrollPerPixel);
    m_frozen->setFrameShape(QFrame::NoFrame);
    m_frozen->setStyleSheet("QTableView { border: none; border-right: 2px solid #C9D6DF; }");
    m_frozen->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_frozen->setSortingEnabled(true);          // header clicks sort the shared proxy
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
