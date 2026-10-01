#include "protocol.h"

#include <QtTest>

using namespace MonitorProtocol;

class ProtocolTest final : public QObject
{
    Q_OBJECT

private slots:
    void crcKnownVector();
    void packetRoundTrip();
    void fragmentedPacket();
    void stickyPackets();
    void corruptedPacketRecovery();
    void telemetryRoundTrip();
    void invalidLengthRecovery() {
        for (int type = 1; type <= 5; ++type) {
            QByteArray bad = encodePacket({static_cast<MessageType>(type), 1, 0, {}}).left(HeaderSize);
            bad[10] = 0; bad[11] = 0x10; bad[12] = 0; bad[13] = 0; // 1 MiB
            const auto good = encodePacket({MessageType::HeartbeatAck, 2, 0, {}});
            StreamParser parser;
            QVERIFY(parser.append(bad).isEmpty());
            QVERIFY(!parser.takeLastError().isEmpty());
            const auto recovered = parser.append(good);
            QCOMPARE(recovered.size(), 1);
            QCOMPARE(recovered.first().sequence, quint32(2));
        }
    }
    void everyTelemetrySplit() {
        Telemetry t; t.timestamp = QDateTime::currentDateTime();
        const auto frame = encodePacket({MessageType::Telemetry, 10, 1, encodeTelemetry(t)});
        for (qsizetype split = 1; split < frame.size(); ++split) {
            StreamParser parser;
            QVERIFY(parser.append(frame.left(split)).isEmpty());
            QCOMPARE(parser.append(frame.mid(split)).size(), 1);
        }
    }
};

void ProtocolTest::crcKnownVector()
{
    QCOMPARE(crc16(QByteArray::fromHex("010300000002")), quint16(0x0BC4));
}

void ProtocolTest::packetRoundTrip()
{
    Packet source{MessageType::Command, 42, 3, encodeCommand(DeviceCommand::Start)};
    StreamParser parser;
    const auto packets = parser.append(encodePacket(source));
    QCOMPARE(packets.size(), 1);
    QCOMPARE(packets.first().sequence, quint32(42));
    QCOMPARE(packets.first().deviceId, quint16(3));
    QCOMPARE(packets.first().payload, source.payload);
}

void ProtocolTest::fragmentedPacket()
{
    const QByteArray frame = encodePacket({MessageType::Heartbeat, 7, 0, {}});
    StreamParser parser;
    QVERIFY(parser.append(frame.left(5)).isEmpty());
    QVERIFY(parser.append(frame.mid(5, 4)).isEmpty());
    QCOMPARE(parser.append(frame.mid(9)).size(), 1);
}

void ProtocolTest::stickyPackets()
{
    const QByteArray first = encodePacket({MessageType::Heartbeat, 1, 0, {}});
    const QByteArray second = encodePacket({MessageType::HeartbeatAck, 2, 0, {}});
    StreamParser parser;
    const auto packets = parser.append(first + second);
    QCOMPARE(packets.size(), 2);
    QCOMPARE(packets.at(0).sequence, quint32(1));
    QCOMPARE(packets.at(1).sequence, quint32(2));
}

void ProtocolTest::corruptedPacketRecovery()
{
    QByteArray bad = encodePacket({MessageType::Heartbeat, 1, 0, {}});
    bad[bad.size() - 1] ^= 0x01;
    const QByteArray good = encodePacket({MessageType::HeartbeatAck, 2, 0, {}});
    StreamParser parser;
    const auto packets = parser.append(bad + good);
    QCOMPARE(packets.size(), 1);
    QCOMPARE(packets.first().sequence, quint32(2));
    QVERIFY(!parser.takeLastError().isEmpty());
}

void ProtocolTest::telemetryRoundTrip()
{
    Telemetry source;
    source.timestamp = QDateTime::fromMSecsSinceEpoch(123456789);
    source.temperature = 26.5;
    source.pressure = 1.23;
    source.rpm = 1800;
    source.state = DeviceState::Running;
    source.faultCode = 0;
    Telemetry decoded;
    QVERIFY(decodeTelemetry(encodeTelemetry(source), &decoded));
    QCOMPARE(decoded.timestamp, source.timestamp);
    QCOMPARE(decoded.temperature, source.temperature);
    QCOMPARE(decoded.pressure, source.pressure);
    QCOMPARE(decoded.rpm, source.rpm);
    QCOMPARE(decoded.state, source.state);
}

QTEST_GUILESS_MAIN(ProtocolTest)
#include "test_protocol.moc"
