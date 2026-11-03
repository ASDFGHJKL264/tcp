#include "databaseworker.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class DatabaseIntegrationTest final : public QObject
{
    Q_OBJECT
private slots:
    void oldHistoryTimeUpgradeIsAtomicAndPreservesOriginalText()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("upgrade.db");
        const auto name = QUuid::createUuid().toString();
        const auto base = QDateTime::currentDateTime().addSecs(-60);
        {
            auto observer = QSqlDatabase::addDatabase("QSQLITE", name);
            observer.setDatabaseName(path); QVERIFY(observer.open());
            QSqlQuery q(observer);
            QVERIFY(q.exec("CREATE TABLE telemetry(id INTEGER PRIMARY KEY,sample_time TEXT,device_id INTEGER,temperature REAL,pressure REAL,rpm INTEGER,state INTEGER,fault_code INTEGER)"));
            QVERIFY(q.exec("CREATE TABLE alarms(id INTEGER PRIMARY KEY,alarm_time TEXT,device_id INTEGER,alarm_type TEXT,message TEXT)"));
            QVERIFY(observer.transaction());
            for (int i = 1; i <= 1101; ++i) {
                const auto time = base.addMSecs(i);
                QVERIFY(q.prepare("INSERT INTO telemetry VALUES(?,?,1,25,1,0,0,0)"));
                q.addBindValue(i);
                q.addBindValue((i % 2 ? time : time.toUTC()).toString(Qt::ISODateWithMs));
                QVERIFY(q.exec());
            }
            for (int i = 1; i <= 2; ++i) {
                QVERIFY(q.prepare("INSERT INTO alarms VALUES(?,?,1,'old','preserve')"));
                q.addBindValue(i);
                q.addBindValue(base.addMSecs(i).toString(Qt::ISODateWithMs));
                QVERIFY(q.exec());
            }
            QVERIFY(observer.commit());
            QVERIFY(q.exec("CREATE TRIGGER reject_time_upgrade BEFORE UPDATE ON alarms WHEN NEW.id=2 BEGIN SELECT RAISE(ABORT,'injected'); END"));
            {
                DatabaseWorker failed;
                QSignalSpy ready(&failed, &DatabaseWorker::ready);
                QSignalSpy errors(&failed, &DatabaseWorker::databaseError);
                failed.initialize(path);
                QCOMPARE(ready.size(), 0);
                QVERIFY(!errors.isEmpty());
            }
            for (const auto &table : {QString("telemetry"), QString("alarms")}) {
                QVERIFY(q.exec("PRAGMA table_info(" + table + ")"));
                while (q.next()) QVERIFY(!q.value(1).toString().endsWith("_epoch_ms"));
            }
            QVERIFY(q.exec("DROP TRIGGER reject_time_upgrade"));
            {
                DatabaseWorker upgraded;
                QSignalSpy ready(&upgraded, &DatabaseWorker::ready);
                upgraded.initialize(path);
                QCOMPARE(ready.size(), 1);
                QVERIFY(q.exec("SELECT id,sample_time,sample_epoch_ms FROM telemetry ORDER BY id"));
                int rows = 0;
                while (q.next()) {
                    const int id = q.value(0).toInt();
                    const auto time = base.addMSecs(id);
                    QCOMPARE(q.value(1).toString(), (id % 2 ? time : time.toUTC()).toString(Qt::ISODateWithMs));
                    QCOMPARE(q.value(2).toLongLong(), time.toMSecsSinceEpoch());
                    ++rows;
                }
                QCOMPARE(rows, 1101);
                QVERIFY(q.exec("SELECT id,alarm_time,alarm_epoch_ms FROM alarms ORDER BY id"));
                rows = 0;
                while (q.next()) {
                    const auto time = base.addMSecs(q.value(0).toInt());
                    QCOMPARE(q.value(1).toString(), time.toString(Qt::ISODateWithMs));
                    QCOMPARE(q.value(2).toLongLong(), time.toMSecsSinceEpoch());
                    ++rows;
                }
                QCOMPARE(rows, 2);
            }
        }
        QSqlDatabase::removeDatabase(name);
    }
    void absoluteTimeHistoryQueriesUseIndexes()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        DatabaseWorker worker;
        QSignalSpy ready(&worker, &DatabaseWorker::ready);
        worker.initialize(directory.filePath("indexes.db"));
        QCOMPARE(ready.size(), 1);
        auto db = QSqlDatabase::database(ready.first()[0].toString());
        for (const auto &table : {QString("telemetry"), QString("alarms")}) {
            const QString instant = table == "telemetry" ? "sample_epoch_ms" : "alarm_epoch_ms";
            for (bool deviceFilter : {false, true}) {
                const QString sql = "EXPLAIN QUERY PLAN SELECT id FROM " + table
                    + " WHERE " + instant + ">=? AND " + instant + "<=?"
                    + (deviceFilter ? " AND device_id=?" : "")
                    + " AND (" + instant + ",id)<(?,?)"
                    + " ORDER BY " + instant + " DESC,id DESC LIMIT 201";
                QSqlQuery q(db);
                QVERIFY(q.prepare(sql));
                q.addBindValue(qint64(1767225600000));
                q.addBindValue(qint64(1767312000000));
                if (deviceFilter) q.addBindValue(2);
                q.addBindValue(qint64(1767312000000));
                q.addBindValue(999);
                QVERIFY(q.exec());
                QString plan;
                while (q.next()) plan += q.value(3).toString();
                QVERIFY2(plan.contains("USING") && plan.contains("INDEX"), qPrintable(plan));
                QVERIFY2(!plan.contains("TEMP B-TREE"), qPrintable(plan));
            }
        }
    }
    void destructionReleasesDatabaseConnection()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QString connection;
        {
            DatabaseWorker worker;
            QSignalSpy ready(&worker, &DatabaseWorker::ready);
            worker.initialize(directory.filePath("scope.db"));
            QCOMPARE(ready.size(), 1);
            connection = ready.first()[0].toString();
            QVERIFY(QSqlDatabase::contains(connection));
        }
        QVERIFY(!QSqlDatabase::contains(connection));
    }
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
