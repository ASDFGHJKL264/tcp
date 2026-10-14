#ifndef REALTIMEPLOT_H
#define REALTIMEPLOT_H

#include <QList>
#include <QPointF>
#include <QWidget>
#include <array>

class RealTimePlot final : public QWidget
{
    Q_OBJECT
public:
    struct AxisRange { double minimum; double maximum; };
    explicit RealTimePlot(QWidget *parent = nullptr);
    void appendPoint(int deviceId, double temperature, double pressure);
    void selectDevice(int deviceId);
    void clear();
    int pointCount(int deviceId) const;
    AxisRange temperatureRange() const;
    AxisRange pressureRange() const;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    struct Series { QList<QPointF> temperature, pressure; qint64 sequence = 0; };
    static AxisRange range(const QList<QPointF> &points, double minimumSpan, double fallbackMax);
    std::array<Series, 4> m_series;
    int m_device = 1;
};

#endif
