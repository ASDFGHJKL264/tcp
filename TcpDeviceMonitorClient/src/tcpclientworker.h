#ifndef TCPCLIENTWORKER_H
#define TCPCLIENTWORKER_H

#include "protocol.h"

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QElapsedTimer>
#include <array>

struct NetworkLogEntry {
    QString message;
    bool warning = false;
};

// One bounded, acknowledged GUI delivery replaces per-frame logs and periodic
// unacknowledged statistics. Monotonic receipt times do not use device clocks.
struct NetworkUiUpdate {
    std::array<qint64, 4> lastTelemetry{{-1, -1, -1, -1}};
    std::array<bool, 4> storageLimited{{false, false, false, false}};
    int pendingTelemetry = 0;
    int peakTelemetry = 0;
    quint64 droppedTelemetry = 0;
    quint64 droppedNormalLogs = 0;
    quint64 droppedWarningLogs = 0;
    QList<NetworkLogEntry> logs;
};
Q_DECLARE_METATYPE(NetworkUiUpdate)

class QTcpSocket;

class TcpClientWorker final : public QObject
{
    Q_OBJECT
public:
    explicit TcpClientWorker(QObject *parent = nullptr);
    static constexpr int MaximumInFlightTelemetry = 64;
    static constexpr int MaximumPendingUiLogs = 128;
    static constexpr int ReservedWarningUiLogs = 16;
    static constexpr int UiLogBatchSize = 32;
    static constexpr int MaximumUiLogCharacters = 4096;
    // Thread-affine diagnostics used by tests; includes the unacknowledged batch.
    int pendingUiLogCount() const;
    quint64 droppedNormalUiLogs() const { return m_droppedNormalUiLogs; }
    quint64 droppedWarningUiLogs() const { return m_droppedWarningUiLogs; }
    // Called in the worker thread after shutdown, before quitting that thread.
    // The caller transfers this finite tail synchronously, not via MetaCall.
    NetworkUiUpdate takeFinalUiUpdate();

public slots:
    void initialize();
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void sendCommand(quint16 deviceId, MonitorProtocol::DeviceCommand command);
    void shutdown();
    // One completion per delivered sample, including failed storage attempts.
    void telemetryProcessed();
    void uiUpdateProcessed();

signals:
    void telemetryReceived(const MonitorProtocol::Telemetry &telemetry);
    void connectionStateChanged(bool connected, const QString &description);
    void uiUpdateReady(const NetworkUiUpdate &update);
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
    void queueLog(const QString &message, bool warning = false);
    void publishUiUpdate();
    NetworkUiUpdate makeUiUpdate() const;
    void clearDeviceActivity();

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
    int m_inFlightTelemetry = 0;
    int m_peakTelemetry = 0;
    quint64 m_droppedTelemetry = 0;
    QTimer *m_uiTimer = nullptr;
    std::array<qint64, 4> m_lastTelemetry{{-1, -1, -1, -1}};
    std::array<bool, 4> m_storageLimited{{false, false, false, false}};
    QList<NetworkLogEntry> m_uiLogs;
    int m_inFlightUiLogs = 0;
    quint64 m_droppedNormalUiLogs = 0;
    quint64 m_droppedWarningUiLogs = 0;
    bool m_uiUpdateInFlight = false;
    bool m_uiDirty = false;
    bool m_uiStopping = false;
    struct Pending {
        quint16 deviceId;
        MonitorProtocol::DeviceCommand command;
        qint64 deadline;
        bool accepted = false;
    };
    QHash<quint32, Pending> m_pending;
};

#endif
