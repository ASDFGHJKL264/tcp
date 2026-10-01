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
        t.temperature = 80; t.pressure = 1;
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TRIGGER reject_alarm BEFORE INSERT ON alarms BEGIN SELECT RAISE(ABORT,'injected'); END"));
            worker.storeTelemetry(t);
            QVERIFY(!completed.takeFirst()[0].toBool());
            QCOMPARE(alarms.size(), 0);
            QVERIFY(q.exec("SELECT COUNT(*) FROM telemetry")); QVERIFY(q.next());
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
        QCOMPARE(alarms.size(), 1); // failed transaction did not consume throttle
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM telemetry")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        }
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

    MonitorProtocol::Telemetry telemetry;
    telemetry.deviceId = 2;
    telemetry.timestamp = QDateTime::currentDateTime();
    telemetry.temperature = 80.0;
    telemetry.pressure = 1.1;
    telemetry.rpm = 1600;
    telemetry.state = MonitorProtocol::DeviceState::Fault;
    telemetry.faultCode = 1001;
    database.storeTelemetry(telemetry);
    database.storeTelemetry(telemetry);
    QCOMPARE(errorSpy.size(), 0);
    QCOMPARE(alarmSpy.size(), 1);

    QSqlQuery query(QSqlDatabase::database(connectionName));
    QVERIFY(query.exec("SELECT COUNT(*) FROM telemetry"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QVERIFY(query.exec("SELECT COUNT(*) FROM alarms"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    query.finish();
    database.shutdown();
}

QTEST_GUILESS_MAIN(DatabaseIntegrationTest)
#include "test_database.moc"
