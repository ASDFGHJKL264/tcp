#include "tcpclientworker.h"
#include "simulatorserver.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QPointer>
#include <QtTest>
#include <limits>
using namespace MonitorProtocol;

// RAII ensures a failed assertion cannot leave a running thread behind.
struct ClientThread {
    QThread thread;
    QPointer<TcpClientWorker> worker = new TcpClientWorker;
    ClientThread() {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
        QMetaObject::invokeMethod(worker, &TcpClientWorker::initialize, Qt::BlockingQueuedConnection);
    }
    void connectTo(quint16 port) {
        QMetaObject::invokeMethod(worker, [w = worker, port] { w->connectToServer("127.0.0.1", port); });
    }
    void command() {
        QMetaObject::invokeMethod(worker, [w = worker] { w->sendCommand(1, DeviceCommand::Stop); });
    }
    void stop() {
        if (!thread.isRunning()) return;
        QMetaObject::invokeMethod(worker, &TcpClientWorker::shutdown, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    ~ClientThread() { stop(); }
};

struct Peer : QTcpServer {
    QTcpSocket *socket = nullptr;
    StreamParser parser;
    QList<Packet> commands;
    bool heartbeat = true;
    Peer() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this] {
                for (const auto &p : parser.append(socket->readAll())) {
                    if (p.type == MessageType::Command) commands.append(p);
                    if (p.type == MessageType::Heartbeat && heartbeat)
                        send({MessageType::HeartbeatAck, p.sequence, 0, {}});
                }
            });
        });
    }
    void send(const Packet &p) { socket->write(encodePacket(p)); }
};

class ReliabilityTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<Telemetry>(); }
    void simulatorClientsHaveIndependentStreamsAndReplies() {
        SimulatorServer server; QVERIFY(server.start(0));
        StreamParser firstParser, secondParser;
        QList<Packet> firstPackets, secondPackets;
        QTcpSocket first, second;
        connect(&first, &QTcpSocket::readyRead, &first, [&] {
            firstPackets.append(firstParser.append(first.readAll()));
        });
        connect(&second, &QTcpSocket::readyRead, &second, [&] {
            secondPackets.append(secondParser.append(second.readAll()));
        });
        QSignalSpy clientCounts(&server, &SimulatorServer::clientCountChanged);
        first.connectToHost(QHostAddress::LocalHost, server.serverPort());
        second.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_COMPARE(first.state(), QAbstractSocket::ConnectedState);
        QTRY_COMPARE(second.state(), QAbstractSocket::ConnectedState);
        QTRY_VERIFY(!clientCounts.isEmpty() && clientCounts.last()[0].toInt() == 2);
        const auto commandAcks = [](const QList<Packet> &packets) {
            QList<Packet> acks;
            for (const auto &packet : packets)
                if (packet.type == MessageType::CommandAck) acks.append(packet);
            return acks;
        };
        // Both connections may use the same command sequence. One partial stream
        // must not consume bytes or receive the acknowledgement of the other.
        const quint32 sequence = 77;
        const auto firstCommand = encodePacket({MessageType::Command, sequence, 1,
                                                encodeCommand(DeviceCommand::Stop)});
        const int split = 8;
        QCOMPARE(first.write(firstCommand.left(split)), qint64(split));
        QTest::qWait(100);
        const auto secondCommand = encodePacket({MessageType::Command, sequence, 1,
                                                 encodeCommand(DeviceCommand::Start)});
        QCOMPARE(second.write(secondCommand), qint64(secondCommand.size()));
        QTRY_COMPARE(commandAcks(secondPackets).size(), 1);
        QCOMPARE(commandAcks(firstPackets).size(), 0);
        QCOMPARE(first.write(firstCommand.mid(split)), qint64(firstCommand.size() - split));
        QTRY_COMPARE(commandAcks(firstPackets).size(), 1);
        QCOMPARE(commandAcks(secondPackets).size(), 1);
        for (const auto &ack : {commandAcks(firstPackets).first(), commandAcks(secondPackets).first()}) {
            QCOMPARE(ack.sequence, sequence);
            QCOMPARE(ack.deviceId, quint16(1));
            QCOMPARE(ack.payload, QByteArray(1, 1));
        }
        const auto sawStoppedDeviceOne = [](const QList<Packet> &packets) {
            for (const auto &packet : packets) {
                if (packet.type != MessageType::Telemetry || packet.deviceId != 1) continue;
                Telemetry telemetry;
                if (decodeTelemetry(packet.payload, &telemetry)
                    && telemetry.state == DeviceState::Stopped) return true;
            }
            return false;
        };
        // Device state is intentionally shared: the completed Stop from the
        // first client must be observed by both monitoring connections.
        QTRY_VERIFY(sawStoppedDeviceOne(firstPackets));
        QTRY_VERIFY(sawStoppedDeviceOne(secondPackets));
        first.abort(); second.abort(); server.stop();
    }
    void boundedTelemetryAndResume() {
        Peer peer; QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
        ClientThread client;
        QSignalSpy samples(client.worker, &TcpClientWorker::telemetryReceived);
        QSignalSpy stats(client.worker, &TcpClientWorker::pipelineStats);
        client.connectTo(peer.serverPort()); QTRY_VERIFY(peer.socket);
        Telemetry t; t.timestamp = QDateTime::currentDateTime();
        for (int i = 0; i < 1000; ++i)
            peer.send({MessageType::Telemetry, quint32(i + 1), 1, encodeTelemetry(t)});
        QTRY_COMPARE(samples.size(), TcpClientWorker::MaximumInFlightTelemetry);
        QTRY_VERIFY(!stats.isEmpty() && stats.last()[2].toULongLong() == 936);
        QCOMPARE(stats.last()[0].toInt(), 64);
        QCOMPARE(stats.last()[1].toInt(), 64);
        // Control confirmation must still progress while telemetry delivery is full.
        QSignalSpy finished(client.worker, &TcpClientWorker::commandFinished);
        client.command(); QTRY_COMPARE(peer.commands.size(), 1);
        peer.send({MessageType::CommandAck, peer.commands.first().sequence, 1, QByteArray(1, 1)});
        peer.send({MessageType::Telemetry, 1001, 1, encodeTelemetry(t)});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first()[2].toBool());
        QMetaObject::invokeMethod(client.worker, &TcpClientWorker::telemetryProcessed,
                                  Qt::BlockingQueuedConnection);
        peer.send({MessageType::Telemetry, 1002, 1, encodeTelemetry(t)});
        QTRY_COMPARE(samples.size(), 65);
    }
    void ackMatchingAndState() {
        Peer peer; QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
        ClientThread client;
        QSignalSpy finished(client.worker, &TcpClientWorker::commandFinished);
        QSignalSpy ack(client.worker, &TcpClientWorker::commandAcknowledged);
        client.connectTo(peer.serverPort());
        QTRY_VERIFY(peer.socket);
        client.command();
        QTRY_COMPARE(peer.commands.size(), 1);
        auto p = peer.commands.first();
        peer.send({MessageType::CommandAck, p.sequence + 1, 1, QByteArray(1, 1)});
        peer.send({MessageType::CommandAck, p.sequence, 2, QByteArray(1, 1)});
        peer.send({MessageType::CommandAck, p.sequence, 1, QByteArray(2, 1)});
        QTest::qWait(150); QCOMPARE(ack.size(), 0); QCOMPARE(finished.size(), 0);
        client.command(); // Same device cannot have overlapping controls.
        QTest::qWait(150); QCOMPARE(peer.commands.size(), 1);
        peer.send({MessageType::CommandAck, p.sequence, 1, QByteArray(1, 1)});
        peer.send({MessageType::CommandAck, p.sequence, 1, QByteArray(1, 1)});
        QTRY_COMPARE(ack.size(), 1); QCOMPARE(finished.size(), 0);
        Telemetry t; t.deviceId = 1; t.timestamp = QDateTime::currentDateTime();
        t.state = DeviceState::Running;
        peer.send({MessageType::Telemetry, 30, 1, encodeTelemetry(t)});
        QTest::qWait(150); QCOMPARE(finished.size(), 0);
        t.state = DeviceState::Stopped;
        peer.send({MessageType::Telemetry, 31, 1, encodeTelemetry(t)});
        QTRY_COMPARE(finished.size(), 1); QVERIFY(finished.first()[2].toBool());
        client.stop(); QVERIFY(client.worker.isNull());
    }
    void timeout_data() {
        QTest::addColumn<bool>("accepted");
        QTest::newRow("missing-ACK") << false;
        QTest::newRow("accepted-without-state") << true;
    }
    void timeout() {
        QFETCH(bool, accepted);
        Peer peer; QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
        ClientThread client;
        QSignalSpy finished(client.worker, &TcpClientWorker::commandFinished);
        QSignalSpy ack(client.worker, &TcpClientWorker::commandAcknowledged);
        client.connectTo(peer.serverPort()); QTRY_VERIFY(peer.socket);
        client.command(); QTRY_COMPARE(peer.commands.size(), 1);
        const auto p = peer.commands.first();
        if (accepted) peer.send({MessageType::CommandAck, p.sequence, 1, QByteArray(1, 1)});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 6500);
        QVERIFY(!finished.first()[2].toBool());
        QVERIFY(finished.first()[3].toString().contains(accepted ? "状态确认超时" : "ACK超时"));
        QCOMPARE(peer.commands.size(), 1); // No automatic control retry.
        const int previousAcks = ack.size();
        Telemetry t; t.deviceId = 1; t.timestamp = QDateTime::currentDateTime();
        t.state = DeviceState::Stopped;
        peer.send({MessageType::CommandAck, p.sequence, 1, QByteArray(1, 1)});
        peer.send({MessageType::Telemetry, 30, 1, encodeTelemetry(t)});
        QTest::qWait(150);
        QCOMPARE(finished.size(), 1); QCOMPARE(ack.size(), previousAcks);
        QCOMPARE(peer.commands.size(), 1); // Late traffic cannot replay a command.

        client.command(); // Only an explicit user request creates another command.
        QTRY_COMPARE(peer.commands.size(), 2);
        const auto next = peer.commands.last();
        QVERIFY(next.sequence != p.sequence);
        peer.send({MessageType::CommandAck, p.sequence, 1, QByteArray(1, 1)});
        peer.send({MessageType::Telemetry, 31, 1, encodeTelemetry(t)});
        QTest::qWait(150);
        QCOMPARE(finished.size(), 1); QCOMPARE(ack.size(), previousAcks);
        peer.send({MessageType::CommandAck, next.sequence, 1, QByteArray(1, 1)});
        peer.send({MessageType::Telemetry, 32, 1, encodeTelemetry(t)});
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(finished.last()[2].toBool());
        QCOMPARE(finished.last()[1].toUInt(), next.sequence);
        QCOMPARE(ack.size(), previousAcks + 1);
        QCOMPARE(peer.commands.size(), 2);
    }
    void rejection() {
        Peer peer; QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
        ClientThread client;
        QSignalSpy finished(client.worker, &TcpClientWorker::commandFinished);
        client.connectTo(peer.serverPort()); QTRY_VERIFY(peer.socket);
        client.command(); QTRY_COMPARE(peer.commands.size(), 1);
        peer.send({MessageType::CommandAck, peer.commands.first().sequence, 1, QByteArray(1, 0)});
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.first()[2].toBool());
    }
    void reconnectAndCancel() {
        SimulatorServer server; QVERIFY(server.start(0));
        const auto port = server.serverPort();
        ClientThread client;
        QSignalSpy telemetry(client.worker, &TcpClientWorker::telemetryReceived);
        client.connectTo(port); QTRY_VERIFY(telemetry.size() >= 4);
        server.stop(); QVERIFY(server.start(port));
        telemetry.clear(); QTRY_VERIFY_WITH_TIMEOUT(telemetry.size() >= 4, 4000);
        server.stop();
        QMetaObject::invokeMethod(client.worker, &TcpClientWorker::disconnectFromServer, Qt::BlockingQueuedConnection);
        QVERIFY(server.start(port)); telemetry.clear();
        QTest::qWait(1500); QCOMPARE(telemetry.size(), 0);
        client.stop(); QVERIFY(client.worker.isNull());
    }
    void pendingDisconnect() {
        Peer peer; QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
        ClientThread client;
        QSignalSpy finished(client.worker, &TcpClientWorker::commandFinished);
        client.connectTo(peer.serverPort()); QTRY_VERIFY(peer.socket);
        client.command(); QTRY_COMPARE(peer.commands.size(), 1);
        peer.socket->abort();
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.first()[2].toBool());
        client.stop(); QVERIFY(client.worker.isNull());
    }
    void cancelWhileConnecting() {
        ClientThread client;
        QSignalSpy active(client.worker, &TcpClientWorker::sessionActiveChanged);
        QMetaObject::invokeMethod(client.worker, [w = client.worker] {
            w->connectToServer("192.0.2.1", 45454);
            w->disconnectFromServer();
        }, Qt::BlockingQueuedConnection);
        QTRY_VERIFY(!active.isEmpty());
        QVERIFY(!active.last()[0].toBool());
        client.stop(); QVERIFY(client.worker.isNull());
    }
    void parserBoundsAndRecovery() {
        StreamParser parser;
        QVERIFY(parser.append(QByteArray(MaximumBufferedBytes + 1, 'x')).isEmpty());
        QCOMPARE(parser.bufferedBytes(), 0);
        QVERIFY(!parser.takeLastError().isEmpty());
        auto good = encodePacket({MessageType::Heartbeat, 10, 0, {}});
        auto bad = good; bad[bad.size() - 1] ^= 1;
        const auto parsed = parser.append(bad + good);
        QCOMPARE(parsed.size(), 1); QCOMPARE(parsed.first().sequence, 10u);
        Telemetry t; t.timestamp = QDateTime::currentDateTime();
        t.temperature = std::numeric_limits<double>::quiet_NaN();
        Telemetry output; QVERIFY(!decodeTelemetry(encodeTelemetry(t), &output));
    }
    void heartbeatTimeout() {
        Peer peer; peer.heartbeat = false; QVERIFY(peer.listen(QHostAddress::LocalHost, 0));
        ClientThread client;
        QSignalSpy states(client.worker, &TcpClientWorker::connectionStateChanged);
        client.connectTo(peer.serverPort()); QTRY_VERIFY(peer.socket);
        QTRY_VERIFY_WITH_TIMEOUT(peer.socket->state() == QAbstractSocket::UnconnectedState, 9000);
        QVERIFY(states.size() >= 2);
        client.stop();
    }
};
QTEST_GUILESS_MAIN(ReliabilityTest)
#include "test_reliability.moc"
