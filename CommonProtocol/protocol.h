#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace MonitorProtocol {

constexpr quint16 Magic = 0xAA55;
constexpr quint8 Version = 1;
constexpr qsizetype HeaderSize = 14;
constexpr quint32 MaximumPayloadSize = 1024 * 1024;
constexpr qsizetype MaximumBufferedBytes = MaximumPayloadSize + HeaderSize + 2 + 64 * 1024;

enum class MessageType : quint8 {
    Heartbeat = 1,
    HeartbeatAck = 2,
    Telemetry = 3,
    Command = 4,
    CommandAck = 5
};

enum class DeviceState : quint8 { Stopped = 0, Running = 1, Fault = 2 };
enum class DeviceCommand : quint8 { Start = 1, Stop = 2, Reset = 3 };

struct Packet {
    MessageType type = MessageType::Heartbeat;
    quint32 sequence = 0;
    quint16 deviceId = 0;
    QByteArray payload;
};

struct Telemetry {
    quint16 deviceId = 0;
    QDateTime timestamp;
    double temperature = 0.0;
    double pressure = 0.0;
    quint32 rpm = 0;
    DeviceState state = DeviceState::Stopped;
    quint16 faultCode = 0;
};

quint16 crc16(const QByteArray &data);
QByteArray encodePacket(const Packet &packet);
QByteArray encodeTelemetry(const Telemetry &telemetry);
bool decodeTelemetry(const QByteArray &payload, Telemetry *telemetry, QString *error = nullptr);
QByteArray encodeCommand(DeviceCommand command);
bool decodeCommand(const QByteArray &payload, DeviceCommand *command);
QString stateText(DeviceState state);

class StreamParser
{
public:
    QList<Packet> append(const QByteArray &bytes);
    void clear();
    qsizetype bufferedBytes() const;
    QString takeLastError();

private:
    QByteArray m_buffer;
    QString m_lastError;
};

}

Q_DECLARE_METATYPE(MonitorProtocol::Telemetry)
Q_DECLARE_METATYPE(MonitorProtocol::DeviceCommand)

#endif
