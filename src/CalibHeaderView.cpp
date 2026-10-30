#include "CalibHeaderView.h"
#include <QMouseEvent>
#include <QPainter>

CalibHeaderView::CalibHeaderView(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setMouseTracking(true);
}

int CalibHeaderView::bandHeight() const
{
    return fontMetrics().height() + 8;
}

QSize CalibHeaderView::sizeHint() const
{
    QSize s = QHeaderView::sizeHint();
    s.rheight() += bandHeight();
    return s;
}

void CalibHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    const int band = bandHeight();
    const QRect nameRect(rect.left(), rect.top(), rect.width(), rect.height() - band);
    painter->save();
    QHeaderView::paintSection(painter, nameRect, logicalIndex);
    painter->restore();

    const QRect bandRect(rect.left(), rect.bottom() - band + 1, rect.width(), band);
    painter->save();
    painter->fillRect(bandRect, QColor("#F7F9FB"));
    painter->setPen(QColor("#C9D6DF"));
    painter->drawLine(bandRect.topLeft(), bandRect.topRight());
    painter->drawLine(bandRect.topRight(), bandRect.bottomRight());
    painter->drawLine(bandRect.bottomLeft(), bandRect.bottomRight());

    const QString text = model() ? model()->headerData(logicalIndex, Qt::Horizontal, CalibRole).toString()
                                 : QString();
    if (!text.isEmpty()) {
        QFont f = painter->font();
        f.setBold(true);
        painter->setFont(f);
        QColor c("#6B7178");                       // label / N — not calibrated
        if (text == "P") c = QColor("#1565C0");    // blue  — Phenology
        else if (text == "G") c = QColor("#2E7D32");   // green — Growth
        painter->setPen(c);
        const bool letter = text.size() == 1;
        painter->drawText(bandRect.adjusted(6, 0, -6, 0),
                          Qt::AlignVCenter | (letter ? Qt::AlignHCenter : Qt::AlignLeft), text);
    }
    painter->restore();
}

// Clicks in the band choose a type; they must not sort the table like a header click
void CalibHeaderView::mousePressEvent(QMouseEvent *e)
{
    if (!inBand(e->position().toPoint())) return QHeaderView::mousePressEvent(e);
    const int col = logicalIndexAt(e->position().toPoint());
    if (col >= 0 && model()) {
        const QString t = model()->headerData(col, Qt::Horizontal, CalibRole).toString();
        if (t.size() == 1)
            emit calibrationClicked(col, mapToGlobal(QPoint(sectionViewportPosition(col), height())));
    }
    e->accept();
}

void CalibHeaderView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!inBand(e->position().toPoint())) return QHeaderView::mouseReleaseEvent(e);
    e->accept();
}

void CalibHeaderView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!inBand(e->position().toPoint())) return QHeaderView::mouseDoubleClickEvent(e);
    e->accept();
}
