#include "simulatorserver.h"
#include "tcpclientworker.h"

#include <QSignalSpy>
#include <QtTest>

class NetworkIntegrationTest final : public QObject
{
    Q_OBJECT
private slots:
    void receivesTelemetryAndCommandAck();
};

void NetworkIntegrationTest::receivesTelemetryAndCommandAck()
{
    qRegisterMetaType<MonitorProtocol::Telemetry>();
    SimulatorServer server;
    QVERIFY(server.start(0));
    TcpClientWorker client;
    client.initialize();
    QSignalSpy telemetrySpy(&client, &TcpClientWorker::telemetryReceived);
    QSignalSpy commandSpy(&client, &TcpClientWorker::commandAcknowledged);
    client.connectToServer(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(telemetrySpy.size() >= 4, 3000);
    client.sendCommand(1, MonitorProtocol::DeviceCommand::Stop);
    QTRY_COMPARE_WITH_TIMEOUT(commandSpy.size(), 1, 2000);
    QCOMPARE(commandSpy.first().at(0).toUInt(), 1u);
    QCOMPARE(commandSpy.first().at(1).toBool(), true);
    client.shutdown();
    server.stop();
}

QTEST_GUILESS_MAIN(NetworkIntegrationTest)
#include "test_networkintegration.moc"
