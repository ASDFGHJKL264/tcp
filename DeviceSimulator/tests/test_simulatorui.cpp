#include "simulatorwindow.h"
#include "simulatorserver.h"
#include <QPushButton>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class SimulatorUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void closingWindowDisconnectsActiveClients()
    {
        auto window = std::make_unique<SimulatorWindow>();
        auto *server = window->findChild<SimulatorServer *>();
        QVERIFY(server);
        server->stop();
        QVERIFY(server->start(0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server->serverPort());
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        QTRY_VERIFY(client.bytesAvailable() > 0);
        window.reset();
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
    }
    void occupiedStartupPortCanBeChangedAndRetried()
    {
        // If another program already occupies 45454, leave it untouched; otherwise
        // hold the port ourselves. The simulator must display a failed start honestly.
        QTcpServer occupied;
        occupied.listen(QHostAddress::AnyIPv4, 45454);
        SimulatorWindow window;
        auto *server = window.findChild<SimulatorServer *>();
        auto *port = window.findChild<QSpinBox *>();
        QPushButton *toggle = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text().contains("服务")) toggle = button;
        QVERIFY(server && port && toggle);
        QVERIFY(!server->isListening());
        QCOMPARE(toggle->text(), QString("启动服务"));
        QVERIFY(port->isEnabled());
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::AnyIPv4, 0));
        const auto availablePort = probe.serverPort();
        probe.close();
        port->setValue(availablePort);
        toggle->click();
        QVERIFY(server->isListening());
        QCOMPARE(server->serverPort(), availablePort);
        QCOMPARE(toggle->text(), QString("停止服务"));
        QVERIFY(!port->isEnabled());
        toggle->click();
        QVERIFY(!server->isListening());
        QVERIFY(port->isEnabled());
    }
};
QTEST_MAIN(SimulatorUiTest)
#include "test_simulatorui.moc"
