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
