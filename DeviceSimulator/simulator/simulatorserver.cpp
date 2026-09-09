#include "simulatorserver.h"

#include <QRandomGenerator>
#include <QTcpSocket>
#include <QtMath>

using namespace MonitorProtocol;

SimulatorServer::SimulatorServer(QObject *parent) : QObject(parent)
{
    for (quint16 id = 1; id <= 4; ++id)
        m_devices.insert(id, Device{DeviceState::Running, 0, id * 0.7});
    m_telemetryTimer.setInterval(500);
    connect(&m_server, &QTcpServer::newConnection, this, &SimulatorServer::acceptClient);
    connect(&m_telemetryTimer, &QTimer::timeout, this, &SimulatorServer::sendTelemetry);
}

bool SimulatorServer::start(quint16 port)
{
    if (m_server.isListening())
        return true;
    if (!m_server.listen(QHostAddress::AnyIPv4, port)) {
        emit logMessage(QStringLiteral("监听失败：%1").arg(m_server.errorString()));
        return false;
    }
    m_telemetryTimer.start();
    emit logMessage(QStringLiteral("模拟服务端已监听 0.0.0.0:%1").arg(port));
    return true;
}

void SimulatorServer::stop()
{
    m_telemetryTimer.stop();
    const auto clients = m_clients; // abort emits disconnected synchronously.
    for (QTcpSocket *socket : clients)
        socket->abort();
    m_server.close();
    emit logMessage(QStringLiteral("模拟服务端已停止"));
}

bool SimulatorServer::isListening() const { return m_server.isListening(); }
quint16 SimulatorServer::serverPort() const { return m_server.serverPort(); }

void SimulatorServer::injectFault(quint16 deviceId, quint16 faultCode)
{
    if (!m_devices.contains(deviceId))
        return;
    Device &device = m_devices[deviceId];
    device.state = DeviceState::Fault;
    device.faultCode = faultCode;
    emit logMessage(QStringLiteral("已向设备%1注入故障码%2").arg(deviceId).arg(faultCode));
}

void SimulatorServer::acceptClient()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (m_clients.size() >= 16) { socket->abort(); socket->deleteLater(); continue; }
        socket->setReadBufferSize(128 * 1024);
        m_clients.append(socket);
        m_parsers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readClient(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { removeClient(socket); });
        emit logMessage(QStringLiteral("客户端接入：%1:%2")
                            .arg(socket->peerAddress().toString()).arg(socket->peerPort()));
        emit clientCountChanged(m_clients.size());
    }
}

void SimulatorServer::readClient(QTcpSocket *socket)
{
    auto &parser = m_parsers[socket];
    const auto packets = parser.append(socket->read(64 * 1024));
    const QString error = parser.takeLastError();
    if (!error.isEmpty())
        emit logMessage(QStringLiteral("协议告警：%1").arg(error));
    for (const Packet &packet : packets)
        handlePacket(socket, packet);
    if (socket->bytesAvailable() > 0)
        QTimer::singleShot(0, socket, [this, socket] {
            if (m_parsers.contains(socket)) readClient(socket);
        });
}

void SimulatorServer::handlePacket(QTcpSocket *socket, const Packet &packet)
{
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    if (socket->bytesToWrite() > 64 * 1024) { socket->abort(); return; }
    if (packet.type == MessageType::Heartbeat) {
        socket->write(encodePacket({MessageType::HeartbeatAck, packet.sequence, 0, {}}));
        return;
    }
    if (packet.type != MessageType::Command || !m_devices.contains(packet.deviceId))
        return;
    DeviceCommand command;
    if (!decodeCommand(packet.payload, &command))
        return;
    Device &device = m_devices[packet.deviceId];
    if (command == DeviceCommand::Start && device.faultCode != 0) {
        socket->write(encodePacket({MessageType::CommandAck, packet.sequence,
                                    packet.deviceId, QByteArray(1, char(0))}));
        return;
    }
    if (command == DeviceCommand::Start)
        device.state = DeviceState::Running;
    else if (command == DeviceCommand::Stop)
        device.state = DeviceState::Stopped;
    else {
        device.faultCode = 0;
        device.state = DeviceState::Stopped;
    }
    socket->write(encodePacket({MessageType::CommandAck, packet.sequence,
                                packet.deviceId, QByteArray(1, char(1))}));
    emit logMessage(QStringLiteral("设备%1执行命令%2")
                        .arg(packet.deviceId).arg(static_cast<int>(command)));
}

void SimulatorServer::sendTelemetry()
{
    if (m_clients.isEmpty())
        return;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        it->phase += 0.08;
        Telemetry data;
        data.deviceId = it.key();
        data.timestamp = QDateTime::currentDateTime();
        data.state = it->state;
        data.faultCode = it->faultCode;
        data.temperature = 24.0 + it.key() + 4.0 * qSin(it->phase)
            + QRandomGenerator::global()->bounded(-20, 21) / 100.0;
        data.pressure = 1.0 + it.key() * 0.05 + 0.12 * qSin(it->phase * 0.6);
        data.rpm = it->state == DeviceState::Running
            ? 1400 + it.key() * 100 + qRound(80 * qSin(it->phase)) : 0;
        const QByteArray frame = encodePacket({MessageType::Telemetry, ++m_sequence,
                                               it.key(), encodeTelemetry(data)});
        const auto clients = m_clients;
        for (QTcpSocket *socket : clients) {
            if (socket->bytesToWrite() + frame.size() > 64 * 1024) socket->abort();
            else socket->write(frame);
        }
    }
}

void SimulatorServer::removeClient(QTcpSocket *socket)
{
    emit logMessage(QStringLiteral("客户端断开"));
    m_clients.removeOne(socket);
    m_parsers.remove(socket);
    socket->deleteLater();
    emit clientCountChanged(m_clients.size());
}
