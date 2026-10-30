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

    // Let the user drag column edges (frozen columns included), like a spreadsheet.
    // Widths the user sets are remembered per scope (e.g. crop) and column name,
    // also across launches; other columns fit their contents after each reload.
    void enableUserColumnWidths(const QString &tableName);
    void setColumnWidthScope(const QString &scope) { m_widthScope = scope; }

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
    void applyColumnWidths();
    QString widthKey(int column) const;

    QTableView *m_frozen;
    int         m_count;
    QString     m_widthTable;
    QString     m_widthScope;
    bool        m_applyingWidths = false;
};

#endif // FROZENTABLEVIEW_H
