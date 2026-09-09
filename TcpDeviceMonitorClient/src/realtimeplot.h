#ifndef REALTIMEPLOT_H
#define REALTIMEPLOT_H

#include <QList>
#include <QPointF>
#include <QWidget>

class RealTimePlot final : public QWidget
{
    Q_OBJECT
public:
    explicit RealTimePlot(QWidget *parent = nullptr);
    void appendPoint(double temperature, double pressure);
    void clear();
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPointF> m_temperature;
    QList<QPointF> m_pressure;
    int m_sequence = 0;
};

#endif
