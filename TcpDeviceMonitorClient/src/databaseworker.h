#ifndef DATABASEWORKER_H
#define DATABASEWORKER_H

#include "protocol.h"

#include <QObject>
#include <QHash>
#include <QString>

class DatabaseWorker final : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseWorker(QObject *parent = nullptr);

public slots:
    void initialize(const QString &databasePath);
    void storeTelemetry(const MonitorProtocol::Telemetry &telemetry);
    void shutdown();

signals:
    void ready(const QString &connectionName);
    void databaseError(const QString &message);
    void alarmRaised(const QString &message);

private:
    QString m_connectionName;
    QString m_databasePath;
    bool m_ready = false;
    QHash<QString, qint64> m_lastAlarmMs;
};

#endif
