#include "historydialog.h"
#include "historyqueryworker.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

namespace {

QStringList historyConnections()
{
    QStringList names;
    for (const auto &name : QSqlDatabase::connectionNames())
        if (name.startsWith("history-query-")) names.append(name);
    names.sort();
    return names;
}

bool seedDatabase(const QString &path, const QDateTime &timestamp)
{
    const QString name = QUuid::createUuid().toString();
    bool success = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            success = query.exec("CREATE TABLE telemetry(id INTEGER PRIMARY KEY, sample_time TEXT, "
                                 "device_id INTEGER, temperature REAL, pressure REAL, rpm INTEGER, "
                                 "state INTEGER, fault_code INTEGER)")
                && query.exec("CREATE TABLE alarms(id INTEGER PRIMARY KEY, alarm_time TEXT, "
                              "device_id INTEGER, alarm_type TEXT, message TEXT)")
                && query.exec("CREATE INDEX idx_telemetry_time ON telemetry(sample_time)")
                && query.exec("CREATE INDEX idx_alarm_time ON alarms(alarm_time)")
                && db.transaction();
            for (int device = 1; success && device <= 2; ++device) {
                for (int i = 1; success && i <= 220; ++i) {
                    success = query.prepare("INSERT INTO telemetry VALUES(NULL,?,?,25.25,1.125,?,2,7)");
                    query.addBindValue(timestamp.toString(Qt::ISODateWithMs));
                    query.addBindValue(device);
                    query.addBindValue(i);
                    success = success && query.exec();
                    success = success && query.prepare("INSERT INTO alarms VALUES(NULL,?,?,?,?)");
                    query.addBindValue(timestamp.toString(Qt::ISODateWithMs));
                    query.addBindValue(device);
                    query.addBindValue(QStringLiteral("设备故障"));
                    query.addBindValue(QString::number(i));
                    success = success && query.exec();
                }
            }
            success = success && db.commit();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return success;
}

class WorkerThread
{
public:
    explicit WorkerThread(const QString &path)
        : control(std::make_shared<HistoryQueryControl>()), worker(new HistoryQueryWorker(path, control))
    {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }
    ~WorkerThread()
    {
        control->stopping.store(true);
        thread.requestInterruption();
        thread.quit();
        thread.wait();
    }
    bool queue(const HistoryQueryRequest &request)
    {
        return QMetaObject::invokeMethod(worker, "query", Qt::QueuedConnection,
                                         Q_ARG(HistoryQueryRequest, request));
    }
    bool submit(const HistoryQueryRequest &request)
    {
        control->latestRequest[request.kind].store(request.requestId);
        return queue(request);
    }
    std::shared_ptr<HistoryQueryControl> control;
    HistoryQueryWorker *worker;
    QThread thread;
};

HistoryQueryRequest requestFor(const QDateTime &time, quint64 id = 1, int kind = 0)
{
    return {id, kind, time.addSecs(-1), time.addSecs(1), 2, {}};
}

}

class HistoryQueryTest final : public QObject
{
    Q_OBJECT
private slots:
    void workerFiltersFormatsAndPages()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto time = QDateTime::currentDateTime().addSecs(-60);
        const auto path = directory.filePath("history.db");
        QVERIFY(seedDatabase(path, time));
        const auto baseline = historyConnections();
        {
            WorkerThread runner(path);
            QSignalSpy completed(runner.worker, &HistoryQueryWorker::completed);
            auto request = requestFor(time);
            QVERIFY(runner.submit(request));
            QTRY_COMPARE(completed.size(), 1);
            auto page = qvariant_cast<HistoryQueryResult>(completed.takeFirst().at(0));
            QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
            QCOMPARE(page.rows.size(), HistoryQueryWorker::PageSize);
            QVERIFY(page.hasMore);
            QCOMPARE(page.rows.first().at(1), QString("2"));
            QCOMPARE(page.rows.first().at(2), QString("25.3"));
            QCOMPARE(page.rows.first().at(3), QString("1.13"));
            QCOMPARE(page.rows.first().at(4), QString("220"));
            QCOMPARE(page.rows.first().at(5), QStringLiteral("故障"));
            QCOMPARE(page.rows.first().at(6), QString("7"));
            QCOMPARE(page.rows.last().at(4), QString("21"));
            request.requestId = 2;
            request.cursor = page.nextCursor;
            QVERIFY(runner.submit(request));
            QTRY_COMPARE(completed.size(), 1);
            page = qvariant_cast<HistoryQueryResult>(completed.takeFirst().at(0));
            QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
            QCOMPARE(page.rows.size(), 20);
            QVERIFY(!page.hasMore);
            QCOMPARE(page.rows.first().at(4), QString("20"));
            QCOMPARE(page.rows.last().at(4), QString("1"));
            request = requestFor(time, 3, 1);
            QVERIFY(runner.submit(request));
            QTRY_COMPARE(completed.size(), 1);
            page = qvariant_cast<HistoryQueryResult>(completed.takeFirst().at(0));
            QVERIFY(page.error.isEmpty());
            QCOMPARE(page.rows.size(), 200);
            QVERIFY(page.hasMore);
            QCOMPARE(page.rows.first().at(2), QStringLiteral("设备故障"));
            QCOMPARE(page.rows.last().at(3), QString("21"));
            request = requestFor(time.addSecs(10), 4);
            QVERIFY(runner.submit(request));
            QTRY_COMPARE(completed.size(), 1);
            page = qvariant_cast<HistoryQueryResult>(completed.takeFirst().at(0));
            QVERIFY(page.error.isEmpty());
            QVERIFY(page.rows.isEmpty());
            QVERIFY(!page.hasMore);
        }
        QCOMPARE(historyConnections(), baseline);
    }

    void skipsSupersededRequests()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto time = QDateTime::currentDateTime();
        const auto path = directory.filePath("history.db");
        QVERIFY(seedDatabase(path, time));
        WorkerThread runner(path);
        QSignalSpy completed(runner.worker, &HistoryQueryWorker::completed);
        runner.control->latestRequest[0].store(2);
        QVERIFY(runner.queue(requestFor(time, 1)));
        QVERIFY(runner.queue(requestFor(time, 2)));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(qvariant_cast<HistoryQueryResult>(completed.at(0).at(0)).requestId, quint64(2));
        QTest::qWait(30);
        QCOMPARE(completed.size(), 1);
    }

    void missingDatabaseIsReadOnly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("does-not-exist.db");
        WorkerThread runner(path);
        QSignalSpy completed(runner.worker, &HistoryQueryWorker::completed);
        QVERIFY(runner.submit(requestFor(QDateTime::currentDateTime())));
        QTRY_COMPARE(completed.size(), 1);
        const auto page = qvariant_cast<HistoryQueryResult>(completed.at(0).at(0));
        QVERIFY(!page.error.isEmpty());
        QVERIFY(page.rows.isEmpty());
        QVERIFY(!QFileInfo::exists(path));
    }

    void dialogPagingAndLatestFilter()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto time = QDateTime::currentDateTime().addSecs(-60);
        const auto path = directory.filePath("history.db");
        QVERIFY(seedDatabase(path, time));
        const auto baseline = historyConnections();
        {
            HistoryDialog dialog(path);
            auto *device = dialog.findChild<QComboBox *>("historyDevice");
            auto *query = dialog.findChild<QPushButton *>("historyQuery");
            auto *telemetry = dialog.findChild<QTableWidget *>("telemetryTable");
            auto *alarms = dialog.findChild<QTableWidget *>("alarmsTable");
            auto *next = dialog.findChild<QPushButton *>("telemetryNext");
            auto *previous = dialog.findChild<QPushButton *>("telemetryPrevious");
            QVERIFY(device && query && telemetry && alarms && next && previous);
            device->setCurrentIndex(1);
            query->click();
            device->setCurrentIndex(2);
            query->click(); // Results queued for old filters must not overwrite this request.
            QTRY_COMPARE(telemetry->rowCount(), 200);
            QTRY_COMPARE(alarms->rowCount(), 200);
            for (int i = 0; i < telemetry->rowCount(); ++i)
                QCOMPARE(telemetry->item(i, 1)->text(), QString("2"));
            QVERIFY(next->isEnabled());
            QVERIFY(!previous->isEnabled());
            next->click();
            QTRY_COMPARE(telemetry->rowCount(), 20);
            QVERIFY(!next->isEnabled());
            QVERIFY(previous->isEnabled());
            QCOMPARE(telemetry->item(0, 4)->text(), QString("20"));
            previous->click();
            QTRY_COMPARE(telemetry->rowCount(), 200);
            QCOMPARE(telemetry->item(0, 4)->text(), QString("220"));
            // Paging uses the last submitted filter, not unsubmitted editor changes.
            device->setCurrentIndex(1);
            next->click();
            QTRY_COMPARE(telemetry->rowCount(), 20);
            QCOMPARE(telemetry->item(0, 1)->text(), QString("2"));
            query->click();
            QTRY_COMPARE(telemetry->rowCount(), 200);
            QCOMPARE(telemetry->item(0, 1)->text(), QString("1"));
        }
        QCOMPARE(historyConnections(), baseline);
    }

    void lockedDatabaseKeepsGuiResponsiveAndCloses()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("history.db");
        QVERIFY(seedDatabase(path, QDateTime::currentDateTime().addSecs(-60)));
        const auto baseline = historyConnections();
        const auto name = QUuid::createUuid().toString();
        {
            auto blocker = QSqlDatabase::addDatabase("QSQLITE", name);
            blocker.setDatabaseName(path);
            QVERIFY(blocker.open());
            QSqlQuery lock(blocker);
            QVERIFY(lock.exec("BEGIN EXCLUSIVE"));
            auto dialog = std::make_unique<HistoryDialog>(path);
            bool heartbeat = false;
            QTimer::singleShot(0, [&heartbeat] { heartbeat = true; });
            QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 1000);
            auto *status = dialog->findChild<QLabel *>("telemetryStatus");
            QVERIFY(status);
            QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("查询失败")), 2000);
            dialog->findChild<QPushButton *>("historyQuery")->click();
            QTest::qWait(25);
            QElapsedTimer closing;
            closing.start();
            dialog.reset();
            QVERIFY2(closing.elapsed() < 1500, "Closing a locked history reader took too long");
            QCOMPARE(historyConnections(), baseline);
            QVERIFY(lock.exec("ROLLBACK"));
        }
        QSqlDatabase::removeDatabase(name);
    }

    void repeatedImmediateCloseReleasesConnections()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("history.db");
        QVERIFY(seedDatabase(path, QDateTime::currentDateTime().addSecs(-60)));
        const auto baseline = historyConnections();
        for (int i = 0; i < 25; ++i) {
            auto dialog = std::make_unique<HistoryDialog>(path);
            dialog->findChild<QPushButton *>("historyQuery")->click();
            dialog.reset();
            QCOMPARE(historyConnections(), baseline);
        }
    }
};

QTEST_MAIN(HistoryQueryTest)
#include "test_history.moc"
