#ifndef SPEGRAPHWIDGET_H
#define SPEGRAPHWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QColor>

struct SpeGraphSeries {
    QString label;
    QVector<QPointF> points;
};

enum class SpeGraphType { Line, StackedBar };

struct SpeGraphData {
    QString title;
    QVector<SpeGraphSeries> series;
    QString xAxisLabel;
    QString yAxisLabel;
    QStringList barCategories;            // organ labels for StackedBar X-axis
    SpeGraphType type = SpeGraphType::Line;
};

class SpeGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpeGraphWidget(QWidget *parent = nullptr);
    ~SpeGraphWidget() override;

    void setData(const SpeGraphData &data);
    void clearData();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    SpeGraphData m_data;
    bool m_hasData = false;

    static const QVector<QColor> SERIES_COLORS;
};

#endif // SPEGRAPHWIDGET_H
