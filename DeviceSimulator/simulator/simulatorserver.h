#ifndef SIMULATORSERVER_H
#define SIMULATORSERVER_H

#include "protocol.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTimer>

class QTcpSocket;

class SimulatorServer final : public QObject
{
    Q_OBJECT
public:
    explicit SimulatorServer(QObject *parent = nullptr);
    bool start(quint16 port);
    void stop();
    bool isListening() const;
    quint16 serverPort() const;

public slots:
    void injectFault(quint16 deviceId, quint16 faultCode = 1001);

signals:
    void logMessage(const QString &message);
    void clientCountChanged(int count);

private:
    struct Device {
        MonitorProtocol::DeviceState state = MonitorProtocol::DeviceState::Stopped;
        quint16 faultCode = 0;
        double phase = 0.0;
    };
    void acceptClient();
    void readClient(QTcpSocket *socket);
    void handlePacket(QTcpSocket *socket, const MonitorProtocol::Packet &packet);
    void sendTelemetry();
    void removeClient(QTcpSocket *socket);

    QTcpServer m_server;
    QTimer m_telemetryTimer;
    QList<QTcpSocket *> m_clients;
    QHash<QTcpSocket *, MonitorProtocol::StreamParser> m_parsers;
    QHash<quint16, Device> m_devices;
    quint32 m_sequence = 0;
};

#endif
