#ifndef CALIBHEADERVIEW_H
#define CALIBHEADERVIEW_H

#include <QHeaderView>

// Column header with an extra band under the names showing each parameter's
// calibration type (P/G/N), like the "!Calibration" line under "@VAR#" in a
// CUL file. The model supplies the band text through CalibRole.
class CalibHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    static const int CalibRole = Qt::UserRole + 2;

    explicit CalibHeaderView(QWidget *parent = nullptr);
    QSize sizeHint() const override;

signals:
    // A click in the band of a column that has a calibration type
    void calibrationClicked(int logicalIndex, const QPoint &globalPos);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    int  bandHeight() const;
    bool inBand(const QPoint &pos) const { return pos.y() >= height() - bandHeight(); }
    bool bandClick(const QPoint &pos) const;
};

#endif // CALIBHEADERVIEW_H
