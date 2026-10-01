#include "databaseworker.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QUuid>

DatabaseWorker::DatabaseWorker(QObject *parent) : QObject(parent) { m_clock.start(); }

void DatabaseWorker::initialize(const QString &databasePath)
{
    Q_ASSERT(QThread::currentThread() == thread());
    shutdown();
    m_databasePath = databasePath;
    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    m_connectionName = QStringLiteral("monitor-db-%1").arg(QUuid::createUuid().toString());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(databasePath);
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=250");
    if (!db.open()) {
        emit databaseError(db.lastError().text());
        return;
    }
    QSqlQuery query(db);
    if (!query.exec("PRAGMA journal_mode=WAL") || !query.exec("PRAGMA synchronous=NORMAL")) {
        emit databaseError(query.lastError().text());
        return;
    }
    const QString createTelemetry =
        "CREATE TABLE IF NOT EXISTS telemetry("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, sample_time TEXT NOT NULL,"
        "device_id INTEGER NOT NULL, temperature REAL NOT NULL, pressure REAL NOT NULL,"
        "rpm INTEGER NOT NULL, state INTEGER NOT NULL, fault_code INTEGER NOT NULL)";
    const QString createAlarm =
        "CREATE TABLE IF NOT EXISTS alarms("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, alarm_time TEXT NOT NULL,"
        "device_id INTEGER NOT NULL, alarm_type TEXT NOT NULL, message TEXT NOT NULL)";
    if (!query.exec(createTelemetry) || !query.exec(createAlarm)) {
        emit databaseError(query.lastError().text());
        return;
    }
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_time ON telemetry(sample_time)")
        || !query.exec("CREATE INDEX IF NOT EXISTS idx_alarm_time ON alarms(alarm_time)")) {
        emit databaseError(query.lastError().text());
        return;
    }
    m_ready = true;
    emit ready(m_connectionName);
}

void DatabaseWorker::storeTelemetry(const MonitorProtocol::Telemetry &t)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QElapsedTimer timer;
    timer.start();
    bool saved = false;
    const auto completion = qScopeGuard([&] { emit sampleProcessed(saved, timer.elapsed()); });
    if (!m_ready) {
        emit databaseError(QStringLiteral("数据库尚未就绪，样本未保存"));
        return;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        emit databaseError(db.lastError().text());
        return;
    }
    const auto rollback = qScopeGuard([&] { if (!saved) db.rollback(); });
    QSqlQuery query(db);
    query.prepare("INSERT INTO telemetry(sample_time,device_id,temperature,pressure,rpm,state,fault_code) "
                  "VALUES(?,?,?,?,?,?,?)");
    query.addBindValue(t.timestamp.toString(Qt::ISODateWithMs));
    query.addBindValue(t.deviceId);
    query.addBindValue(t.temperature);
    query.addBindValue(t.pressure);
    query.addBindValue(t.rpm);
    query.addBindValue(static_cast<int>(t.state));
    query.addBindValue(t.faultCode);
    if (!query.exec()) {
        emit databaseError(query.lastError().text());
        return;
    }
    QString alarmType;
    QString message;
    if (t.temperature < 0.0 || t.temperature > 70.0) {
        alarmType = QStringLiteral("温度越限");
        message = QStringLiteral("设备%1温度%2℃越限").arg(t.deviceId).arg(t.temperature, 0, 'f', 1);
    } else if (t.pressure < 0.7 || t.pressure > 1.5) {
        alarmType = QStringLiteral("压力越限");
        message = QStringLiteral("设备%1压力%2MPa越限").arg(t.deviceId).arg(t.pressure, 0, 'f', 2);
    } else if (t.faultCode != 0) {
        alarmType = QStringLiteral("设备故障");
        message = QStringLiteral("设备%1故障码：%2").arg(t.deviceId).arg(t.faultCode);
    }
    const QString alarmKey = QStringLiteral("%1:%2").arg(t.deviceId).arg(alarmType);
    // Rate limiting uses local monotonic time, never a device's adjustable clock.
    const qint64 alarmTime = m_clock.elapsed();
    const bool raiseAlarm = !alarmType.isEmpty() && (!m_lastAlarmMs.contains(alarmKey)
        || alarmTime - m_lastAlarmMs.value(alarmKey) >= 60000);
    if (raiseAlarm) {
        query.prepare("INSERT INTO alarms(alarm_time,device_id,alarm_type,message) VALUES(?,?,?,?)");
        query.addBindValue(t.timestamp.toString(Qt::ISODateWithMs));
        query.addBindValue(t.deviceId);
        query.addBindValue(alarmType);
        query.addBindValue(message);
        if (!query.exec()) {
            emit databaseError(query.lastError().text());
            return;
        }
    }
    if (!db.commit()) {
        emit databaseError(db.lastError().text());
        return;
    }
    saved = true;
    if (raiseAlarm) {
        m_lastAlarmMs.insert(alarmKey, alarmTime);
        emit alarmRaised(message);
    }
}

void DatabaseWorker::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_connectionName.isEmpty()) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            if (db.isValid())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_ready = false;
    m_connectionName.clear();
    m_lastAlarmMs.clear();
}
