#ifndef FROZENTABLEVIEW_H
#define FROZENTABLEVIEW_H

#include <QTableView>

// QTableView whose first N columns stay in place while the rest scroll
// horizontally (VAR#, VRNAME, ... next to dozens of parameters). A second view
// over the left edge shares the model, selection model and sorting.
class FrozenTableView : public QTableView
{
    Q_OBJECT

public:
    // calibrationBand: headers get a P/G/N band (CalibHeaderView) under the names
    explicit FrozenTableView(int frozenColumns, bool calibrationBand = false, QWidget *parent = nullptr);

    void setModel(QAbstractItemModel *model) override;
    void setSelectionModel(QItemSelectionModel *selectionModel) override;
    // Like setItemDelegateForColumn, but also for the frozen view's copy of the column
    void setColumnDelegate(int column, QAbstractItemDelegate *delegate);

signals:
    // A click in the calibration band of a parameter column (either header)
    void calibrationClicked(int column, const QPoint &globalPos);

protected:
    void resizeEvent(QResizeEvent *event) override;
    QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers) override;
    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override;
    void updateGeometries() override;

private:
    void syncColumns();          // hide all but the frozen columns, match widths
    void updateFrozenGeometry();
    int  frozenWidth() const;

    QTableView *m_frozen;
    int         m_count;
};

#endif // FROZENTABLEVIEW_H
