#include "asyncfilelogger.h"
#include "realtimeplot.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>

class PresentationTest final : public QObject
{
    Q_OBJECT
private slots:
    void independentAxesAndDeviceHistory()
    {
        RealTimePlot plot;
        plot.appendPoint(1, -20, 3);
        plot.appendPoint(1, 150, 4);
        auto t = plot.temperatureRange(), p = plot.pressureRange();
        QVERIFY(t.minimum < -20 && t.maximum > 150);
        QVERIFY(p.minimum < 3 && p.maximum > 4);
        plot.appendPoint(2, 25, 1);
        plot.selectDevice(2);
        QCOMPARE(plot.pointCount(1), 2);
        QCOMPARE(plot.pointCount(2), 1);
        QVERIFY(plot.temperatureRange().minimum < 25);
        plot.selectDevice(1);
        QCOMPARE(plot.temperatureRange().maximum, t.maximum);
        plot.resize(700, 280);
        QVERIFY(!plot.grab().isNull());
    }
    void boundedPlotAndInvalidInput()
    {
        RealTimePlot plot;
        for (int n = 0; n < 400; ++n) plot.appendPoint(1, n, n / 100.0);
        QCOMPARE(plot.pointCount(1), 300);
        plot.appendPoint(1, std::numeric_limits<double>::quiet_NaN(), 1);
        plot.appendPoint(0, 25, 1);
        QCOMPARE(plot.pointCount(1), 300);
        plot.clear();
        QCOMPARE(plot.pointCount(1), 0);
    }
    void extremeFiniteValuesRender()
    {
        RealTimePlot plot;
        plot.appendPoint(1, -1e308, 1e308);
        plot.appendPoint(1, 1e308, -1e308);
        const auto t = plot.temperatureRange();
        QVERIFY(std::isfinite(t.minimum) && std::isfinite(t.maximum));
        QVERIFY(t.minimum <= -1e308 && t.maximum >= 1e308);
        plot.resize(700, 280);
        QVERIFY(!plot.grab().isNull());
    }
    void loggerBoundsPreservesWarningAndDrains()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("logs/monitor.log");
        AsyncFileLogger logger(path);
        for (int n = 0; n < 1000; ++n) logger.append(QString("normal-%1").arg(n));
        QVERIFY(logger.pendingLines() <= AsyncFileLogger::MaximumPendingLines);
        QVERIFY(logger.droppedNormal() > 0);
        QVERIFY(logger.append("WARN-important", true));
        logger.shutdown();
        QCOMPARE(logger.pendingLines(), 0);
        QCOMPARE(logger.failedLines(), quint64(0));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("WARN-important"));
    }
    void loggerFailureReportedWithoutRecursion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile blocker(dir.filePath("not-directory"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        AsyncFileLogger logger(dir.filePath("not-directory/log.txt"));
        QSignalSpy errors(&logger, &AsyncFileLogger::writeFailed);
        logger.append("entry", true);
        logger.shutdown();
        QCOMPARE(logger.failedLines(), quint64(1));
        QCOMPARE(errors.count(), 1);
    }
    void loggerRotationAndRepeatedShutdown()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("monitor.log");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(5 * 1024 * 1024));
        file.close();
        AsyncFileLogger logger(path);
        logger.append("rotated");
        logger.shutdown();
        logger.shutdown();
        QVERIFY(QFile::exists(path + ".1"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("rotated\n"));
    }
};

QTEST_MAIN(PresentationTest)
#include "test_presentation.moc"
