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
    explicit FrozenTableView(int frozenColumns, QWidget *parent = nullptr);

    void setModel(QAbstractItemModel *model) override;
    void setSelectionModel(QItemSelectionModel *selectionModel) override;

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
