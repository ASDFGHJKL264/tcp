#ifndef TCPCLIENTWORKER_H
#define TCPCLIENTWORKER_H

#include "protocol.h"

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QElapsedTimer>

class QTcpSocket;

class TcpClientWorker final : public QObject
{
    Q_OBJECT
public:
    explicit TcpClientWorker(QObject *parent = nullptr);

public slots:
    void initialize();
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void sendCommand(quint16 deviceId, MonitorProtocol::DeviceCommand command);
    void shutdown();

signals:
    void telemetryReceived(const MonitorProtocol::Telemetry &telemetry);
    void connectionStateChanged(bool connected, const QString &description);
    void logMessage(const QString &message, bool warning = false);
    void commandAcknowledged(quint16 deviceId, bool success);
    void sessionActiveChanged(bool active);
    void commandFinished(quint16 deviceId, quint32 sequence, bool success, const QString &reason);

private:
    void ensureSocket();
    void attemptConnection();
    void readAvailableData();
    void handlePacket(const MonitorProtocol::Packet &packet);
    void sendHeartbeat();
    void scheduleReconnect();
    void checkDeadlines();
    void failPending(const QString &reason);
    bool writePacket(const MonitorProtocol::Packet &packet);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    MonitorProtocol::StreamParser m_parser;
    QString m_host;
    quint16 m_port = 0;
    quint32 m_sequence = 0;
    qint64 m_lastHeartbeatAck = 0;
    int m_reconnectAttempt = 0;
    bool m_manualDisconnect = true;
    QTimer *m_deadlineTimer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_connectStarted = 0;
    quint32 m_heartbeatSequence = 0;
    struct Pending {
        quint16 deviceId;
        MonitorProtocol::DeviceCommand command;
        qint64 deadline;
        bool accepted = false;
    };
    QHash<quint32, Pending> m_pending;
};

#endif
