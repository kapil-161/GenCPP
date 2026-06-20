#include "SpeGraphWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

SpeGraphWidget::SpeGraphWidget(QWidget *parent)
    : QWidget(parent), m_hasData(false)
{
    setMinimumSize(300, 200);
}

SpeGraphWidget::~SpeGraphWidget()
{
}

void SpeGraphWidget::setData(const SpeGraphData &data)
{
    m_data = data;
    m_hasData = !data.points.isEmpty();
    
    // Ensure points are sorted by X for a line graph
    if (m_hasData) {
        std::sort(m_data.points.begin(), m_data.points.end(), [](const QPointF &a, const QPointF &b) {
            return a.x() < b.x();
        });
    }
    
    update();
}

void SpeGraphWidget::clearData()
{
    m_hasData = false;
    m_data.points.clear();
    m_data.title.clear();
    update();
}

void SpeGraphWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Fill background
    painter.fillRect(rect(), QColor("#FFFFFF"));

    if (!m_hasData || m_data.points.size() < 2) {
        painter.setPen(QColor("#999999"));
        painter.drawText(rect(), Qt::AlignCenter, "Click a line containing temperature data\n(e.g., FNPGT, XLMAXT) to view graph.");
        return;
    }

    // Determine min/max
    double minX = m_data.points.first().x();
    double maxX = m_data.points.last().x();
    double minY = m_data.points.first().y();
    double maxY = m_data.points.first().y();

    for (const auto &p : std::as_const(m_data.points)) {
        if (p.x() < minX) minX = p.x();
        if (p.x() > maxX) maxX = p.x();
        if (p.y() < minY) minY = p.y();
        if (p.y() > maxY) maxY = p.y();
    }

    // Add some padding to extremes
    double rangeX = maxX - minX;
    double rangeY = maxY - minY;
    
    if (rangeX == 0) rangeX = 1.0;
    if (rangeY == 0) rangeY = 1.0;

    minX -= rangeX * 0.05;
    maxX += rangeX * 0.05;
    minY -= rangeY * 0.1;
    maxY += rangeY * 0.1;

    // Define graph area margins
    int marginLeft = 40;
    int marginRight = 20;
    int marginTop = 30;
    int marginBottom = 30;

    QRect graphRect(marginLeft, marginTop, 
                    width() - marginLeft - marginRight, 
                    height() - marginTop - marginBottom);

    // Draw Title
    if (!m_data.title.isEmpty()) {
        painter.setPen(Qt::black);
        QFont titleFont = painter.font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(QRect(0, 5, width(), marginTop - 5), Qt::AlignHCenter | Qt::AlignTop, m_data.title);
        titleFont.setBold(false);
        painter.setFont(titleFont);
    }

    // Draw Axes
    painter.setPen(QPen(Qt::black, 1));
    painter.drawLine(graphRect.bottomLeft(), graphRect.bottomRight()); // X axis
    painter.drawLine(graphRect.topLeft(), graphRect.bottomLeft());     // Y axis

    // Draw grid and labels
    painter.setPen(QPen(QColor("#E0E0E0"), 1, Qt::DashLine));
    
    // Simple 5 vertical grid lines
    for (int i = 0; i <= 4; ++i) {
        double xVal = minX + (maxX - minX) * i / 4.0;
        int xPos = graphRect.left() + (graphRect.width() * i / 4.0);
        
        painter.drawLine(xPos, graphRect.top(), xPos, graphRect.bottom());
        
        painter.setPen(Qt::black);
        painter.drawText(QRect(xPos - 25, graphRect.bottom() + 5, 50, 20), 
                         Qt::AlignHCenter | Qt::AlignTop, QString::number(xVal, 'f', 1));
        painter.setPen(QPen(QColor("#E0E0E0"), 1, Qt::DashLine));
    }

    // Simple 4 horizontal grid lines
    for (int i = 0; i <= 4; ++i) {
        double yVal = minY + (maxY - minY) * i / 4.0;
        int yPos = graphRect.bottom() - (graphRect.height() * i / 4.0);
        
        painter.drawLine(graphRect.left(), yPos, graphRect.right(), yPos);
        
        painter.setPen(Qt::black);
        painter.drawText(QRect(0, yPos - 10, graphRect.left() - 5, 20), 
                         Qt::AlignRight | Qt::AlignVCenter, QString::number(yVal, 'f', 2));
        painter.setPen(QPen(QColor("#E0E0E0"), 1, Qt::DashLine));
    }

    // Draw X and Y Labels
    painter.setPen(Qt::black);
    if (!m_data.xAxisLabel.isEmpty()) {
        painter.drawText(QRect(graphRect.left(), graphRect.bottom() + 15, graphRect.width(), 20),
                         Qt::AlignRight | Qt::AlignBottom, m_data.xAxisLabel);
    }
    
    if (!m_data.yAxisLabel.isEmpty()) {
        painter.save();
        painter.translate(12, graphRect.top() + graphRect.height() / 2);
        painter.rotate(-90);
        painter.drawText(QRect(-graphRect.height()/2, -10, graphRect.height(), 20), 
                         Qt::AlignCenter, m_data.yAxisLabel);
        painter.restore();
    }

    // Draw Line
    QPainterPath path;
    bool first = true;
    QVector<QPointF> screenPoints;

    auto mapToScreen = [&](const QPointF &p) -> QPointF {
        double nx = (p.x() - minX) / (maxX - minX);
        double ny = (p.y() - minY) / (maxY - minY);
        int sx = graphRect.left() + nx * graphRect.width();
        int sy = graphRect.bottom() - ny * graphRect.height();
        return QPointF(sx, sy);
    };

    for (const auto &p : std::as_const(m_data.points)) {
        QPointF sp = mapToScreen(p);
        screenPoints.append(sp);
        if (first) {
            path.moveTo(sp);
            first = false;
        } else {
            path.lineTo(sp);
        }
    }

    painter.setPen(QPen(QColor("#0078D4"), 2));
    painter.drawPath(path);

    // Draw Points
    painter.setBrush(QColor("#0078D4"));
    for (const auto &sp : std::as_const(screenPoints)) {
        painter.drawEllipse(sp, 3, 3);
    }
}
