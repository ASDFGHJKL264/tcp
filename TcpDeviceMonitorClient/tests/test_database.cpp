#include "databaseworker.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class DatabaseIntegrationTest final : public QObject
{
    Q_OBJECT
private slots:
    void storesTelemetryAndAlarm();
    void alarmThrottleIsIndependentByTypeAndDevice();
    void rollbackAndBusyRecovery() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        DatabaseWorker worker;
        QSignalSpy ready(&worker, &DatabaseWorker::ready);
        QSignalSpy completed(&worker, &DatabaseWorker::sampleProcessed);
        QSignalSpy alarms(&worker, &DatabaseWorker::alarmRaised);
        worker.storeTelemetry({});
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.takeFirst()[0].toBool());
        worker.initialize(directory.filePath("monitor.db"));
        QCOMPARE(ready.size(), 1);
        auto db = QSqlDatabase::database(ready.first()[0].toString());
        MonitorProtocol::Telemetry t;
        t.deviceId = 1; t.timestamp = QDateTime::currentDateTime();
        t.temperature = 80; t.pressure = 2; t.faultCode = 1001;
        {
            QSqlQuery q(db);
            // Fail the final alarm, after temperature and pressure were inserted.
            QVERIFY(q.exec("CREATE TRIGGER reject_alarm BEFORE INSERT ON alarms "
                           "WHEN NEW.alarm_type = '设备故障' "
                           "BEGIN SELECT RAISE(ABORT,'injected'); END"));
            worker.storeTelemetry(t);
            QVERIFY(!completed.takeFirst()[0].toBool());
            QCOMPARE(alarms.size(), 0);
            QVERIFY(q.exec("SELECT COUNT(*) FROM telemetry")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0); q.finish();
            QVERIFY(q.exec("SELECT COUNT(*) FROM alarms")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0); q.finish();
            QVERIFY(q.exec("DROP TRIGGER reject_alarm"));
        }
        {
            auto lock = QSqlDatabase::addDatabase("QSQLITE", "test-lock");
            lock.setDatabaseName(directory.filePath("monitor.db")); QVERIFY(lock.open());
            QSqlQuery q(lock); QVERIFY(q.exec("BEGIN IMMEDIATE"));
            QElapsedTimer timer; timer.start();
            worker.storeTelemetry(t);
            QVERIFY(timer.elapsed() < 1500);
            QVERIFY(!completed.takeFirst()[0].toBool());
            QVERIFY(q.exec("ROLLBACK"));
        }
        QSqlDatabase::removeDatabase("test-lock");
        worker.storeTelemetry(t);
        QVERIFY(completed.takeFirst()[0].toBool());
        QCOMPARE(alarms.size(), 3); // failed transaction did not consume any cooldown
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM telemetry")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT COUNT(*) FROM alarms")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 3);
        }
        worker.storeTelemetry(t);
        QVERIFY(completed.takeFirst()[0].toBool());
        QCOMPARE(alarms.size(), 3); // successful commit activates all three cooldowns
        db = QSqlDatabase();
        worker.shutdown();
    }
};

void DatabaseIntegrationTest::storesTelemetryAndAlarm()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseWorker database;
    QSignalSpy readySpy(&database, &DatabaseWorker::ready);
    QSignalSpy errorSpy(&database, &DatabaseWorker::databaseError);
    QSignalSpy alarmSpy(&database, &DatabaseWorker::alarmRaised);
    database.initialize(directory.filePath("monitor.db"));
    QCOMPARE(errorSpy.size(), 0);
    QCOMPARE(readySpy.size(), 1);
    const QString connectionName = readySpy.first().first().toString();
    const QString observerName = connectionName + QStringLiteral("-observer");
    auto observer = QSqlDatabase::addDatabase("QSQLITE", observerName);
    observer.setDatabaseName(directory.filePath("monitor.db"));
    QVERIFY(observer.open());
    bool allAlarmsCommittedBeforeNotification = true;
    const auto notificationConnection = connect(&database, &DatabaseWorker::alarmRaised,
                                               &database, [&](const QString &) {
        // A different connection cannot see the writes before the transaction commits.
        QSqlQuery query(observer);
        if (!query.exec("SELECT COUNT(*) FROM alarms") || !query.next()
            || query.value(0).toInt() != 3)
            allAlarmsCommittedBeforeNotification = false;
    });

    MonitorProtocol::Telemetry telemetry;
    telemetry.deviceId = 2;
    telemetry.timestamp = QDateTime::currentDateTime();
    telemetry.temperature = 80.0;
    telemetry.pressure = 2.0;
    telemetry.rpm = 1600;
    telemetry.state = MonitorProtocol::DeviceState::Fault;
    telemetry.faultCode = 1001;
    database.storeTelemetry(telemetry);
    database.storeTelemetry(telemetry);
    QCOMPARE(errorSpy.size(), 0);
    QCOMPARE(alarmSpy.size(), 3);
    QVERIFY(allAlarmsCommittedBeforeNotification);

    {
        QSqlQuery query(QSqlDatabase::database(connectionName));
        QVERIFY(query.exec("SELECT COUNT(*) FROM telemetry"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
        QVERIFY(query.exec("SELECT alarm_type FROM alarms ORDER BY id"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), QStringLiteral("温度越限"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), QStringLiteral("压力越限"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), QStringLiteral("设备故障"));
        QVERIFY(!query.next());
    }
    disconnect(notificationConnection);
    observer = QSqlDatabase();
    QSqlDatabase::removeDatabase(observerName);
    database.shutdown();
}

void DatabaseIntegrationTest::alarmThrottleIsIndependentByTypeAndDevice()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    DatabaseWorker database;
    QSignalSpy ready(&database, &DatabaseWorker::ready);
    QSignalSpy alarms(&database, &DatabaseWorker::alarmRaised);
    QSignalSpy completed(&database, &DatabaseWorker::sampleProcessed);
    QSignalSpy errors(&database, &DatabaseWorker::databaseError);
    database.initialize(directory.filePath("monitor.db"));
    QCOMPARE(ready.size(), 1);
    MonitorProtocol::Telemetry t;
    t.deviceId = 1; t.timestamp = QDateTime::currentDateTime();
    t.temperature = 80; t.pressure = 1;
    database.storeTelemetry(t);
    QCOMPARE(alarms.size(), 1);
    t.pressure = 2; t.faultCode = 1001;
    database.storeTelemetry(t);
    QCOMPARE(alarms.size(), 3); // temperature cooldown does not suppress other types
    t.timestamp = t.timestamp.addSecs(-3600);
    database.storeTelemetry(t);
    QCOMPARE(alarms.size(), 3); // device clock changes do not bypass local cooldowns
    t.deviceId = 2;
    database.storeTelemetry(t);
    QCOMPARE(alarms.size(), 6); // another device has independent cooldowns
    QCOMPARE(errors.size(), 0);
    QCOMPARE(completed.size(), 4);
    for (const auto &completion : completed)
        QVERIFY(completion[0].toBool());
    {
        QSqlQuery query(QSqlDatabase::database(ready.first()[0].toString()));
        QVERIFY(query.exec("SELECT device_id,alarm_type,COUNT(*) FROM alarms GROUP BY device_id,alarm_type"));
        int groups = 0;
        while (query.next()) {
            QCOMPARE(query.value(2).toInt(), 1);
            ++groups;
        }
        QCOMPARE(groups, 6);
    }
    database.shutdown();
}

QTEST_GUILESS_MAIN(DatabaseIntegrationTest)
#include "test_database.moc"
