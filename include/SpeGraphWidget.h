#ifndef SPEGRAPHWIDGET_H
#define SPEGRAPHWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>

struct SpeGraphData {
    QString title;
    QVector<QPointF> points;
    QString xAxisLabel;
    QString yAxisLabel;
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
    bool m_hasData;
};

#endif // SPEGRAPHWIDGET_H
