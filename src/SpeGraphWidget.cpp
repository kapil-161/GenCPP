#include "SpeGraphWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFontMetrics>
#include <algorithm>
#include <limits>

const QVector<QColor> SpeGraphWidget::SERIES_COLORS = {
    QColor("#0078D4"),  // blue
    QColor("#E64A19"),  // orange-red
    QColor("#388E3C"),  // green
    QColor("#7B1FA2"),  // purple
};

SpeGraphWidget::SpeGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 200);
}

SpeGraphWidget::~SpeGraphWidget() {}

void SpeGraphWidget::setData(const SpeGraphData &data)
{
    m_data = data;
    m_hasData = false;
    for (auto &s : m_data.series) {
        if (!s.points.isEmpty()) {
            m_hasData = true;
            std::sort(s.points.begin(), s.points.end(),
                      [](const QPointF &a, const QPointF &b){ return a.x() < b.x(); });
        }
    }
    update();
}

void SpeGraphWidget::clearData()
{
    m_data = {};
    m_hasData = false;
    update();
}

void SpeGraphWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#FFFFFF"));

    if (!m_hasData) {
        painter.setPen(QColor("#999999"));
        painter.drawText(rect(), Qt::AlignCenter,
            "Click a data line in the SPE editor to view its graph.\n"
            "(Supports X/Y table pairs, 4-value response functions,\n"
            " interleaved X,Y data, and multi-series tables.)");
        return;
    }

    // Compute global bounds across all series
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto &s : m_data.series) {
        for (const auto &p : s.points) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
    }

    double rangeX = maxX - minX;
    double rangeY = maxY - minY;
    if (rangeX == 0) rangeX = 1.0;
    if (rangeY == 0) rangeY = 1.0;

    minX -= rangeX * 0.05;
    maxX += rangeX * 0.05;
    minY -= rangeY * 0.10;
    maxY += rangeY * 0.10;

    // Margins — extra right space if legend needed
    bool showLegend = m_data.series.size() > 1;
    int marginLeft   = 52;
    int marginRight  = showLegend ? 115 : 20;
    int marginTop    = 28;
    int marginBottom = 36;

    QRect graphRect(marginLeft, marginTop,
                    width()  - marginLeft - marginRight,
                    height() - marginTop  - marginBottom);

    // Title
    if (!m_data.title.isEmpty()) {
        QFont f = painter.font();
        f.setBold(true);
        painter.setFont(f);
        painter.setPen(Qt::black);
        painter.drawText(QRect(0, 4, width() - marginRight, marginTop - 4),
                         Qt::AlignHCenter | Qt::AlignTop, m_data.title);
        f.setBold(false);
        painter.setFont(f);
    }

    // Axes
    painter.setPen(QPen(Qt::black, 1));
    painter.drawLine(graphRect.bottomLeft(), graphRect.bottomRight());
    painter.drawLine(graphRect.topLeft(),    graphRect.bottomLeft());

    // Grid lines + tick labels
    for (int i = 0; i <= 4; ++i) {
        double xVal = minX + (maxX - minX) * i / 4.0;
        int    xPos = graphRect.left() + graphRect.width() * i / 4;
        painter.setPen(QPen(QColor("#E0E0E0"), 1, Qt::DashLine));
        painter.drawLine(xPos, graphRect.top(), xPos, graphRect.bottom());
        painter.setPen(Qt::black);
        painter.drawText(QRect(xPos - 30, graphRect.bottom() + 3, 60, 16),
                         Qt::AlignHCenter | Qt::AlignTop,
                         QString::number(xVal, 'g', 4));
    }
    for (int i = 0; i <= 4; ++i) {
        double yVal = minY + (maxY - minY) * i / 4.0;
        int    yPos = graphRect.bottom() - graphRect.height() * i / 4;
        painter.setPen(QPen(QColor("#E0E0E0"), 1, Qt::DashLine));
        painter.drawLine(graphRect.left(), yPos, graphRect.right(), yPos);
        painter.setPen(Qt::black);
        painter.drawText(QRect(2, yPos - 10, marginLeft - 6, 20),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(yVal, 'g', 4));
    }

    // Axis labels
    painter.setPen(Qt::black);
    if (!m_data.xAxisLabel.isEmpty()) {
        painter.drawText(QRect(graphRect.left(), graphRect.bottom() + 20,
                               graphRect.width(), 14),
                         Qt::AlignHCenter | Qt::AlignTop, m_data.xAxisLabel);
    }
    if (!m_data.yAxisLabel.isEmpty()) {
        painter.save();
        painter.translate(11, graphRect.top() + graphRect.height() / 2);
        painter.rotate(-90);
        painter.drawText(QRect(-graphRect.height() / 2, -10, graphRect.height(), 20),
                         Qt::AlignCenter, m_data.yAxisLabel);
        painter.restore();
    }

    // Map a data point to screen coordinates
    auto toScreen = [&](const QPointF &p) -> QPointF {
        double nx = (p.x() - minX) / (maxX - minX);
        double ny = (p.y() - minY) / (maxY - minY);
        return QPointF(graphRect.left() + nx * graphRect.width(),
                       graphRect.bottom() - ny * graphRect.height());
    };

    // Draw each series
    for (int si = 0; si < m_data.series.size(); ++si) {
        const auto &s = m_data.series[si];
        if (s.points.size() < 2) continue;

        QColor color = SERIES_COLORS[si % SERIES_COLORS.size()];

        QPainterPath path;
        QVector<QPointF> screenPts;
        bool first = true;
        for (const auto &p : s.points) {
            QPointF sp = toScreen(p);
            screenPts.append(sp);
            if (first) { path.moveTo(sp); first = false; }
            else         path.lineTo(sp);
        }

        painter.setPen(QPen(color, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        int dotR = (s.points.size() > 20) ? 2 : 3;  // smaller dots for smooth curves
        for (const auto &sp : screenPts)
            painter.drawEllipse(sp, dotR, dotR);
    }

    // Legend (only when multiple series)
    if (showLegend) {
        int lx = graphRect.right() + 8;
        int ly = graphRect.top() + 4;
        for (int si = 0; si < m_data.series.size(); ++si) {
            QColor c = SERIES_COLORS[si % SERIES_COLORS.size()];
            int rowY = ly + si * 18;
            painter.fillRect(lx, rowY + 2, 14, 10, c);
            painter.setPen(Qt::black);
            painter.drawText(lx + 18, rowY, 94, 14,
                             Qt::AlignLeft | Qt::AlignVCenter,
                             m_data.series[si].label);
        }
    }
}
