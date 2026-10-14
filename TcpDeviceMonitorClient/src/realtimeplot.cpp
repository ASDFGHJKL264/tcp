#include "realtimeplot.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <limits>

RealTimePlot::RealTimePlot(QWidget *parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(250, 250, 250));
    setPalette(palette);
}

void RealTimePlot::appendPoint(int deviceId, double temperature, double pressure)
{
    if (deviceId < 1 || deviceId > 4 || !std::isfinite(temperature) || !std::isfinite(pressure)) return;
    auto &series = m_series[deviceId - 1];
    const auto x = ++series.sequence;
    series.temperature.append(QPointF(x, temperature));
    series.pressure.append(QPointF(x, pressure));
    while (series.temperature.size() > 300) series.temperature.removeFirst();
    while (series.pressure.size() > 300) series.pressure.removeFirst();
    if (m_device == deviceId) update();
}

void RealTimePlot::selectDevice(int deviceId)
{
    if (deviceId < 1 || deviceId > 4) return;
    m_device = deviceId;
    update();
}

void RealTimePlot::clear()
{
    m_series = {};
    update();
}

int RealTimePlot::pointCount(int deviceId) const
{
    return deviceId >= 1 && deviceId <= 4 ? m_series[deviceId - 1].temperature.size() : 0;
}

RealTimePlot::AxisRange RealTimePlot::range(const QList<QPointF> &points, double minimumSpan, double fallbackMax)
{
    if (points.isEmpty()) return {0, fallbackMax};
    long double low = points.first().y(), high = low;
    for (const auto &point : points) {
        low = qMin(low, static_cast<long double>(point.y()));
        high = qMax(high, static_cast<long double>(point.y()));
    }
    const long double center = low + (high - low) / 2;
    const long double span = qMax(high - low,
        qMax(static_cast<long double>(minimumSpan), std::fabs(center) * 0.01L));
    const long double half = span * 0.6L;
    const long double limit = std::numeric_limits<double>::max();
    return {static_cast<double>(qMax(-limit, center - half)),
            static_cast<double>(qMin(limit, center + half))};
}

RealTimePlot::AxisRange RealTimePlot::temperatureRange() const
{ return range(m_series[m_device - 1].temperature, 10, 100); }

RealTimePlot::AxisRange RealTimePlot::pressureRange() const
{ return range(m_series[m_device - 1].pressure, 0.2, 2); }

QSize RealTimePlot::minimumSizeHint() const { return {500, 230}; }

void RealTimePlot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = rect().adjusted(72, 34, -72, -42);
    if (area.width() <= 0 || area.height() <= 0) return;
    const auto temperature = temperatureRange(), pressure = pressureRange();
    const auto label = [](long double value) { return QString::number(static_cast<double>(value), 'g', 4); };
    for (int n = 0; n <= 5; ++n) {
        const qreal y = area.top() + area.height() * n / 5.0;
        const long double fraction = 1 - n / 5.0L;
        painter.setPen(QColor(210, 210, 210));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(QColor(211, 47, 47));
        painter.drawText(QRectF(0, y - 10, 65, 20), Qt::AlignRight | Qt::AlignVCenter,
                        label(temperature.minimum + fraction * (static_cast<long double>(temperature.maximum) - temperature.minimum)));
        painter.setPen(QColor(25, 118, 210));
        painter.drawText(QRectF(area.right() + 7, y - 10, 63, 20), Qt::AlignLeft | Qt::AlignVCenter,
                        label(pressure.minimum + fraction * (static_cast<long double>(pressure.maximum) - pressure.minimum)));
    }
    painter.setPen(QColor(211, 47, 47));
    painter.drawText(QRectF(0, 5, 150, 24), Qt::AlignCenter, QStringLiteral("温度 (℃)"));
    painter.setPen(QColor(25, 118, 210));
    painter.drawText(QRectF(width() - 160, 5, 160, 24), Qt::AlignCenter, QStringLiteral("压力 (MPa)"));
    painter.setPen(Qt::darkGray);
    painter.drawRect(area);
    painter.drawText(QRectF(area.left(), height() - 29, area.width(), 22), Qt::AlignCenter,
                    QStringLiteral("设备%1 · 最近300点 · 采样序号 · 双轴自动缩放").arg(m_device));
    const auto draw = [&](const QList<QPointF> &points, const QColor &color, AxisRange axis) {
        if (points.isEmpty()) return;
        const auto first = points.first().x();
        const auto span = qMax(1.0, points.last().x() - first);
        const long double valueSpan = static_cast<long double>(axis.maximum) - axis.minimum;
        if (valueSpan <= 0) return;
        QPainterPath path;
        for (int n = 0; n < points.size(); ++n) {
            const qreal x = area.left() + (points[n].x() - first) / span * area.width();
            const long double fraction = (static_cast<long double>(points[n].y()) - axis.minimum) / valueSpan;
            const qreal y = area.bottom() - static_cast<qreal>(fraction) * area.height();
            if (!n) path.moveTo(x, y); else path.lineTo(x, y);
        }
        // Distinguish the two physical quantities even when the paths overlap.
        painter.setPen(QPen(color, 2, color == QColor(25, 118, 210) ? Qt::DashLine : Qt::SolidLine));
        painter.save();
        painter.setClipRect(area.adjusted(-1, -1, 1, 1));
        if (points.size() == 1) painter.drawEllipse(path.currentPosition(), 2.5, 2.5);
        else painter.drawPath(path);
        painter.restore();
    };
    const auto &series = m_series[m_device - 1];
    draw(series.temperature, QColor(211, 47, 47), temperature);
    draw(series.pressure, QColor(25, 118, 210), pressure);
}
