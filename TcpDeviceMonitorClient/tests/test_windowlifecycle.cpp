#include "mainwindow.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QTextEdit>
#include <QTableWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

class WindowLifecycleTest : public QObject {
    Q_OBJECT
private slots:
    void storageOverloadDoesNotMarkLiveDeviceOffline() {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCoreApplication::setOrganizationName("TcpReliabilityTests");
        QCoreApplication::setApplicationName(QFileInfo(directory.path()).fileName());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QSettings settings;
        settings.setValue("network/host", "127.0.0.1");
        settings.setValue("network/port", server.serverPort());
        MainWindow window(nullptr, directory.path());
        // Simulate a downstream consumer withholding completion credits. This
        // avoids locking real databases or relying on machine-specific IO timing.
        QVERIFY(QObject::disconnect(&window, SIGNAL(storedTelemetryProcessed()), nullptr, nullptr));
        window.connectConfiguredServer();
        QTRY_VERIFY(server.hasPendingConnections());
        auto *socket = server.nextPendingConnection();
        MonitorProtocol::StreamParser parser;
        connect(socket, &QTcpSocket::readyRead, socket, [&] {
            for (const auto &packet : parser.append(socket->readAll())) {
                if (packet.type == MonitorProtocol::MessageType::Heartbeat)
                    socket->write(MonitorProtocol::encodePacket({MonitorProtocol::MessageType::HeartbeatAck,
                        packet.sequence, 0, {}}));
            }
        });
        MonitorProtocol::Telemetry telemetry;
        telemetry.timestamp = QDateTime::currentDateTime();
        const auto send = [&] {
            socket->write(MonitorProtocol::encodePacket({MonitorProtocol::MessageType::Telemetry,
                1, 1, MonitorProtocol::encodeTelemetry(telemetry)}));
        };
        for (int i = 0; i < 100; ++i) send();
        QTimer stream;
        connect(&stream, &QTimer::timeout, &window, send);
        stream.start(100);
        QTest::qWait(3800);
        auto *table = window.findChild<QTableWidget *>(); QVERIFY(table);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("在线（存储过载）"));
        QVERIFY(table->item(0, 1)->toolTip().contains("最近已交付样本"));
        stream.stop();
        QTest::qWait(4400); // Includes the 1-second GUI offline timer granularity.
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("离线"));
        QCOMPARE(table->item(0, 3)->text(), QStringLiteral("--"));
        socket->deleteLater();
    }
    void openConnectAndClose() {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // Use a unique settings/database namespace: never touch normal app data.
        QCoreApplication::setOrganizationName("TcpReliabilityTests");
        QCoreApplication::setApplicationName(QFileInfo(directory.path()).fileName());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QSettings settings;
        settings.setValue("network/host", "127.0.0.1");
        settings.setValue("network/port", server.serverPort());
        const int cycles = qBound(3, qEnvironmentVariableIntValue("TCP_LIFECYCLE_CYCLES"), 1000);
        qint64 maximumCloseMs = 0;
        for (int i = 0; i < cycles; ++i) {
            auto window = std::make_unique<MainWindow>(nullptr, directory.path());
            window->show();
            window->connectConfiguredServer();
            QTRY_VERIFY(server.hasPendingConnections());
            auto *socket = server.nextPendingConnection();
            QTRY_VERIFY2(window->findChild<QTextEdit *>()->toPlainText().contains("SQLite"),
                         qPrintable(window->findChild<QTextEdit *>()->toPlainText()));
            MonitorProtocol::Telemetry t;
            t.deviceId = 1; t.timestamp = QDateTime::currentDateTime();
            t.temperature = 25; t.pressure = 1;
            socket->write(MonitorProtocol::encodePacket({MonitorProtocol::MessageType::Telemetry,
                1, 1, MonitorProtocol::encodeTelemetry(t)}));
            socket->flush();
            QTest::qWait(100);
            QElapsedTimer closeTimer; closeTimer.start();
            window.reset(); // Both workers shut down while a TCP session is active.
            maximumCloseMs = qMax(maximumCloseMs, closeTimer.elapsed());
            QTRY_COMPARE(socket->state(), QAbstractSocket::UnconnectedState);
            socket->deleteLater();
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "verify-close");
            db.setDatabaseName(directory.filePath("data/monitor.db"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT COUNT(*) FROM telemetry")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), cycles);
        }
        QSqlDatabase::removeDatabase("verify-close");
        qInfo() << "connected close cycles:" << cycles << "maximum close ms:" << maximumCloseMs;
    }
};
QTEST_MAIN(WindowLifecycleTest)
#include "test_windowlifecycle.moc"
