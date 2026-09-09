#include "mainwindow.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QTextEdit>
#include <QtTest>

class WindowLifecycleTest : public QObject {
    Q_OBJECT
private slots:
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
        for (int i = 0; i < 3; ++i) {
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
            window.reset(); // Both workers shut down while a TCP session is active.
            QTRY_COMPARE(socket->state(), QAbstractSocket::UnconnectedState);
            socket->deleteLater();
        }
    }
};
QTEST_MAIN(WindowLifecycleTest)
#include "test_windowlifecycle.moc"
