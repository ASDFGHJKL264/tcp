#include "tcpclientworker.h"

#include <QDateTime>
#include <QTcpSocket>
#include <utility>

using namespace MonitorProtocol;

TcpClientWorker::TcpClientWorker(QObject *parent) : QObject(parent) { m_clock.start(); }

void TcpClientWorker::initialize()
{
    ensureSocket();
}

void TcpClientWorker::ensureSocket()
{
    if (m_socket)
        return;
    m_socket = new QTcpSocket(this);
    m_socket->setReadBufferSize(128 * 1024);
    m_deadlineTimer = new QTimer(this);
    m_deadlineTimer->setInterval(100);
    connect(m_deadlineTimer, &QTimer::timeout, this, &TcpClientWorker::checkDeadlines);
    m_heartbeatTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(2000);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_reconnectTimer->stop();
        m_reconnectAttempt = 0;
        m_lastHeartbeatAck = m_clock.elapsed();
        m_heartbeatTimer->start();
        emit connectionStateChanged(true, QStringLiteral("已连接"));
        emit logMessage(QStringLiteral("TCP连接建立：%1:%2").arg(m_host).arg(m_port));
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        m_heartbeatTimer->stop();
        m_parser.clear();
        m_heartbeatSequence = 0;
        failPending(QStringLiteral("连接断开，执行结果未知；不会自动重发"));
        emit connectionStateChanged(false, m_manualDisconnect
            ? QStringLiteral("已断开") : QStringLiteral("连接中断，等待重连"));
        if (!m_manualDisconnect)
            scheduleReconnect();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpClientWorker::readAvailableData);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit logMessage(QStringLiteral("网络错误：%1").arg(m_socket->errorString()), true);
        if (!m_manualDisconnect && m_socket->state() == QAbstractSocket::UnconnectedState)
            scheduleReconnect();
    });
    connect(m_heartbeatTimer, &QTimer::timeout, this, &TcpClientWorker::sendHeartbeat);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpClientWorker::attemptConnection);
}

void TcpClientWorker::connectToServer(const QString &host, quint16 port)
{
    ensureSocket();
    disconnectFromServer();
    m_host = host.trimmed();
    m_port = port;
    if (m_host.isEmpty() || port == 0) {
        emit logMessage(QStringLiteral("服务器地址或端口无效"), true);
        return;
    }
    m_manualDisconnect = false;
    m_reconnectAttempt = 0;
    m_deadlineTimer->start();
    emit sessionActiveChanged(true);
    attemptConnection();
}

void TcpClientWorker::attemptConnection()
{
    if (m_manualDisconnect || m_host.isEmpty() || m_port == 0)
        return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        return;
    m_parser.clear();
    m_connectStarted = m_clock.elapsed();
    emit connectionStateChanged(false, QStringLiteral("正在连接…"));
    m_socket->connectToHost(m_host, m_port);
}

void TcpClientWorker::disconnectFromServer()
{
    m_manualDisconnect = true;
    if (m_reconnectTimer) m_reconnectTimer->stop();
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (m_deadlineTimer) m_deadlineTimer->stop();
    if (m_socket)
        m_socket->abort();
    m_parser.clear();
    m_heartbeatSequence = 0;
    failPending(QStringLiteral("连接已取消，执行结果未知"));
    emit sessionActiveChanged(false);
    emit connectionStateChanged(false, QStringLiteral("已断开"));
}

void TcpClientWorker::sendCommand(quint16 deviceId, DeviceCommand command)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        emit logMessage(QStringLiteral("命令发送失败：TCP未连接"), true);
        return;
    }
    if (deviceId < 1 || deviceId > 4 || static_cast<int>(command) < 1 || static_cast<int>(command) > 3)
        return;
    for (const auto &pending : std::as_const(m_pending)) {
        if (pending.deviceId == deviceId) {
            emit logMessage(QStringLiteral("设备%1仍有待确认命令").arg(deviceId), true);
            return;
        }
    }
    do { ++m_sequence; } while (m_sequence == 0 || m_pending.contains(m_sequence));
    if (!writePacket({MessageType::Command, m_sequence, deviceId, encodeCommand(command)})) {
        emit commandFinished(deviceId, m_sequence, false, QStringLiteral("命令发送失败"));
        return;
    }
    m_pending.insert(m_sequence, {deviceId, command, m_clock.elapsed() + 3000, false});
    emit logMessage(QStringLiteral("设备%1命令#%2已发送，等待ACK").arg(deviceId).arg(m_sequence));
}

void TcpClientWorker::readAvailableData()
{
    const auto packets = m_parser.append(m_socket->read(64 * 1024));
    const QString parserError = m_parser.takeLastError();
    if (!parserError.isEmpty())
        emit logMessage(QStringLiteral("协议解析告警：%1").arg(parserError), true);
    for (const Packet &packet : packets)
        handlePacket(packet);
    if (m_socket->bytesAvailable() > 0)
        QTimer::singleShot(0, this, &TcpClientWorker::readAvailableData);
}

void TcpClientWorker::handlePacket(const Packet &packet)
{
    if (packet.type == MessageType::HeartbeatAck) {
        if (m_heartbeatSequence != 0 && packet.sequence == m_heartbeatSequence
            && packet.deviceId == 0 && packet.payload.isEmpty()) {
            m_lastHeartbeatAck = m_clock.elapsed();
            m_heartbeatSequence = 0;
        }
        return;
    }
    if (packet.type == MessageType::CommandAck) {
        auto it = m_pending.find(packet.sequence);
        if (it == m_pending.end() || it->deviceId != packet.deviceId || it->accepted
            || m_clock.elapsed() >= it->deadline
            || packet.payload.size() != 1 || (packet.payload[0] != 0 && packet.payload[0] != 1)) return;
        const bool accepted = packet.payload[0] == 1;
        emit commandAcknowledged(packet.deviceId, accepted);
        if (!accepted) {
            m_pending.erase(it);
            emit commandFinished(packet.deviceId, packet.sequence, false, QStringLiteral("设备拒绝命令"));
        } else {
            it->accepted = true;
            it->deadline = m_clock.elapsed() + 5000;
            emit logMessage(QStringLiteral("设备%1接受命令#%2，等待遥测状态确认")
                .arg(packet.deviceId).arg(packet.sequence));
        }
        return;
    }
    if (packet.type != MessageType::Telemetry || packet.deviceId < 1 || packet.deviceId > 4)
        return;
    Telemetry telemetry;
    telemetry.deviceId = packet.deviceId;
    QString error;
    if (!decodeTelemetry(packet.payload, &telemetry, &error)) {
        emit logMessage(error, true);
        return;
    }
    emit telemetryReceived(telemetry);
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->deviceId != telemetry.deviceId || !it->accepted
            || m_clock.elapsed() >= it->deadline) continue;
        const bool matches = it->command == DeviceCommand::Start
            ? telemetry.state == DeviceState::Running && telemetry.faultCode == 0
            : telemetry.state == DeviceState::Stopped
              && (it->command != DeviceCommand::Reset || telemetry.faultCode == 0);
        if (matches) {
            const auto sequence = it.key();
            m_pending.erase(it);
            emit commandFinished(telemetry.deviceId, sequence, true, QStringLiteral("ACK与遥测状态均已确认"));
        }
        break;
    }
}

void TcpClientWorker::sendHeartbeat()
{
    const qint64 now = m_clock.elapsed();
    if (now - m_lastHeartbeatAck > 6500) {
        emit logMessage(QStringLiteral("心跳连续超时，主动断开并重连"), true);
        m_socket->abort();
        return;
    }
    if (m_heartbeatSequence == 0) {
        do { ++m_sequence; } while (m_sequence == 0 || m_pending.contains(m_sequence));
        m_heartbeatSequence = m_sequence;
        writePacket({MessageType::Heartbeat, m_sequence, 0, {}});
    }
}

void TcpClientWorker::scheduleReconnect()
{
    if (m_manualDisconnect || m_reconnectTimer->isActive())
        return;
    m_reconnectAttempt = qMin(m_reconnectAttempt + 1, 32);
    const int delay = qMin(30000, 1000 * (1 << qMin(m_reconnectAttempt - 1, 5)));
    emit logMessage(QStringLiteral("%1秒后进行第%2次重连")
                        .arg(delay / 1000).arg(m_reconnectAttempt), true);
    m_reconnectTimer->start(delay);
    emit connectionStateChanged(false, QStringLiteral("等待重连（%1秒），可取消").arg(delay / 1000));
}

bool TcpClientWorker::writePacket(const Packet &packet)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return false;
    const auto frame = encodePacket(packet);
    if (m_socket->bytesToWrite() + frame.size() > 64 * 1024) {
        emit logMessage(QStringLiteral("发送积压超过64KiB，断开连接"), true);
        m_socket->abort();
        return false;
    }
    return m_socket->write(frame) == frame.size();
}

void TcpClientWorker::checkDeadlines()
{
    const auto now = m_clock.elapsed();
    if (m_socket->state() != QAbstractSocket::ConnectedState
        && m_socket->state() != QAbstractSocket::UnconnectedState && now - m_connectStarted >= 5000) {
        m_socket->abort();
        scheduleReconnect();
    }
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now < it->deadline) { ++it; continue; }
        const auto sequence = it.key();
        const auto pending = it.value();
        it = m_pending.erase(it);
        emit commandFinished(pending.deviceId, sequence, false, pending.accepted
            ? QStringLiteral("ACK已接受，但状态确认超时；不会自动重发")
            : QStringLiteral("ACK超时，执行结果未知；不会自动重发"));
    }
}

void TcpClientWorker::failPending(const QString &reason)
{
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit commandFinished(it->deviceId, it.key(), false, reason);
}

void TcpClientWorker::shutdown()
{
    disconnectFromServer();
    if (m_socket)
        m_socket->abort();
}
