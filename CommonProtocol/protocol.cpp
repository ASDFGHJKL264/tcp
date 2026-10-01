#include "protocol.h"

#include <QDataStream>
#include <QIODevice>
#include <utility>
#include <cmath>

using namespace MonitorProtocol;

quint16 MonitorProtocol::crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (char byte : data) {
        crc ^= static_cast<quint8>(byte);
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

QByteArray MonitorProtocol::encodePacket(const Packet &packet)
{
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << Magic << Version << static_cast<quint8>(packet.type)
        << packet.sequence << packet.deviceId << static_cast<quint32>(packet.payload.size());
    frame.append(packet.payload);
    const quint16 checksum = crc16(frame);
    QDataStream tail(&frame, QIODevice::Append);
    tail.setByteOrder(QDataStream::LittleEndian);
    tail << checksum;
    return frame;
}

QByteArray MonitorProtocol::encodeTelemetry(const Telemetry &telemetry)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << telemetry.timestamp.toMSecsSinceEpoch() << telemetry.temperature
        << telemetry.pressure << telemetry.rpm << static_cast<quint8>(telemetry.state)
        << telemetry.faultCode;
    return payload;
}

bool MonitorProtocol::decodeTelemetry(const QByteArray &payload, Telemetry *telemetry, QString *error)
{
    if (!telemetry || payload.size() != 31) {
        if (error)
            *error = QStringLiteral("遥测负载长度错误：%1").arg(payload.size());
        return false;
    }
    QDataStream in(payload);
    in.setByteOrder(QDataStream::BigEndian);
    qint64 timestamp;
    quint8 state;
    in >> timestamp >> telemetry->temperature >> telemetry->pressure
       >> telemetry->rpm >> state >> telemetry->faultCode;
    telemetry->timestamp = QDateTime::fromMSecsSinceEpoch(timestamp);
    telemetry->state = static_cast<DeviceState>(state);
    if (in.status() != QDataStream::Ok || state > static_cast<quint8>(DeviceState::Fault)
        || !std::isfinite(telemetry->temperature) || !std::isfinite(telemetry->pressure)
        || !telemetry->timestamp.isValid()) {
        if (error)
            *error = QStringLiteral("遥测数据内容无效");
        return false;
    }
    return true;
}

QByteArray MonitorProtocol::encodeCommand(DeviceCommand command)
{
    return QByteArray(1, static_cast<char>(command));
}

bool MonitorProtocol::decodeCommand(const QByteArray &payload, DeviceCommand *command)
{
    if (!command || payload.size() != 1)
        return false;
    const quint8 value = static_cast<quint8>(payload.at(0));
    if (value < 1 || value > 3)
        return false;
    *command = static_cast<DeviceCommand>(value);
    return true;
}

QString MonitorProtocol::stateText(DeviceState state)
{
    switch (state) {
    case DeviceState::Running: return QStringLiteral("运行");
    case DeviceState::Fault: return QStringLiteral("故障");
    default: return QStringLiteral("停止");
    }
}

QList<Packet> StreamParser::append(const QByteArray &bytes)
{
    if (bytes.size() > MaximumBufferedBytes - m_buffer.size()) {
        clear();
        m_lastError = QStringLiteral("接收缓存超限，已丢弃数据");
        return {};
    }
    m_buffer.append(bytes);
    QList<Packet> packets;
    const QByteArray magicBytes = QByteArray::fromHex("aa55");
    while (true) {
        const qsizetype magicIndex = m_buffer.indexOf(magicBytes);
        if (magicIndex < 0) {
            if (m_buffer.size() > 1)
                m_buffer = m_buffer.right(1);
            break;
        }
        if (magicIndex > 0) {
            m_lastError = QStringLiteral("已丢弃%1个无效字节").arg(magicIndex);
            m_buffer.remove(0, magicIndex);
        }
        if (m_buffer.size() < HeaderSize)
            break;
        QDataStream header(m_buffer.left(HeaderSize));
        header.setByteOrder(QDataStream::BigEndian);
        quint16 magic;
        quint8 version;
        quint8 type;
        quint32 sequence;
        quint16 deviceId;
        quint32 payloadLength;
        header >> magic >> version >> type >> sequence >> deviceId >> payloadLength;
        // Version 1 has fixed-size payloads. Reject impossible lengths before
        // waiting for a body, otherwise a corrupt header can block valid frames.
        const quint32 expectedLength = type == 3 ? 31 : (type == 4 || type == 5 ? 1 : 0);
        if (version != Version || type < 1 || type > 5 || payloadLength != expectedLength) {
            m_lastError = QStringLiteral("协议版本或负载长度无效");
            m_buffer.remove(0, 2);
            continue;
        }
        const qsizetype frameSize = HeaderSize + payloadLength + 2;
        if (m_buffer.size() < frameSize)
            break;
        const QByteArray body = m_buffer.left(frameSize - 2);
        QDataStream crcStream(m_buffer.mid(frameSize - 2, 2));
        crcStream.setByteOrder(QDataStream::LittleEndian);
        quint16 receivedCrc;
        crcStream >> receivedCrc;
        if (receivedCrc != crc16(body)) {
            m_lastError = QStringLiteral("CRC校验失败");
            m_buffer.remove(0, 2);
            continue;
        }
        Packet packet;
        packet.type = static_cast<MessageType>(type);
        packet.sequence = sequence;
        packet.deviceId = deviceId;
        packet.payload = m_buffer.mid(HeaderSize, payloadLength);
        packets.append(packet);
        m_buffer.remove(0, frameSize);
    }
    return packets;
}

void StreamParser::clear() { m_buffer.clear(); m_lastError.clear(); }
qsizetype StreamParser::bufferedBytes() const { return m_buffer.size(); }
QString StreamParser::takeLastError() { return std::exchange(m_lastError, {}); }
