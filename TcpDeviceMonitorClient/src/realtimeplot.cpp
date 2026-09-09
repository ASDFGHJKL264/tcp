#include "realtimeplot.h"

#include <QPainter>
#include <QPainterPath>

RealTimePlot::RealTimePlot(QWidget *parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(250, 250, 250));
    setPalette(palette);
}

void RealTimePlot::appendPoint(double temperature, double pressure)
{
    ++m_sequence;
    m_temperature.append(QPointF(m_sequence, temperature));
    m_pressure.append(QPointF(m_sequence, pressure));
    while (m_temperature.size() > 300) m_temperature.removeFirst();
    while (m_pressure.size() > 300) m_pressure.removeFirst();
    update();
}

void RealTimePlot::clear()
{
    m_temperature.clear();
    m_pressure.clear();
    m_sequence = 0;
    update();
}

QSize RealTimePlot::minimumSizeHint() const { return {500, 230}; }

void RealTimePlot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = rect().adjusted(48, 18, -22, -38);
    painter.setPen(QColor(210, 210, 210));
    for (int i = 0; i <= 5; ++i) {
        const qreal y = area.top() + area.height() * i / 5.0;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(Qt::darkGray);
        painter.drawText(QRectF(0, y - 10, 43, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(100 - i * 20));
        painter.setPen(QColor(210, 210, 210));
    }
    painter.setPen(Qt::darkGray);
    painter.drawRect(area);
    painter.setPen(QPen(QColor(211, 47, 47), 2));
    painter.drawLine(area.left(), height() - 17, area.left() + 28, height() - 17);
    painter.drawText(area.left() + 34, height() - 25, 80, 18, Qt::AlignLeft, QStringLiteral("温度℃"));
    painter.setPen(QPen(QColor(25, 118, 210), 2));
    painter.drawLine(area.left() + 110, height() - 17, area.left() + 138, height() - 17);
    painter.drawText(area.left() + 144, height() - 25, 100, 18, Qt::AlignLeft,
                     QStringLiteral("压力×50"));

    auto drawSeries = [&](const QList<QPointF> &points, const QColor &color, double scale) {
        if (points.size() < 2)
            return;
        const double firstX = points.first().x();
        const double span = qMax(1.0, points.last().x() - firstX);
        QPainterPath path;
        for (int i = 0; i < points.size(); ++i) {
            const double x = area.left() + (points.at(i).x() - firstX) / span * area.width();
            const double value = qBound(0.0, points.at(i).y() * scale, 100.0);
            const double y = area.bottom() - value / 100.0 * area.height();
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        painter.setPen(QPen(color, 2));
        painter.drawPath(path);
    };
    drawSeries(m_temperature, QColor(211, 47, 47), 1.0);
    drawSeries(m_pressure, QColor(25, 118, 210), 50.0);
}
