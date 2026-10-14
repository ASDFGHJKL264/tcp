#include "historyqueryworker.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <utility>

HistoryQueryWorker::HistoryQueryWorker(const QString &databasePath,
                                     std::shared_ptr<HistoryQueryControl> control,
                                     QObject *parent)
    : QObject(parent), m_databasePath(databasePath), m_control(std::move(control))
{
    qRegisterMetaType<HistoryQueryRequest>();
    qRegisterMetaType<HistoryQueryResult>();
}

HistoryQueryWorker::~HistoryQueryWorker()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_connectionName.isEmpty()) {
        {
            auto database = QSqlDatabase::database(m_connectionName, false);
            if (database.isValid()) database.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool HistoryQueryWorker::cancelled(const HistoryQueryRequest &request) const
{
    return m_control->stopping.load() || QThread::currentThread()->isInterruptionRequested()
        || m_control->latestRequest[request.kind].load() != request.requestId;
}

QString HistoryQueryWorker::openDatabase()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_connectionName.isEmpty()) {
        m_connectionName = QStringLiteral("history-query-%1").arg(QUuid::createUuid().toString());
        auto database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        database.setDatabaseName(m_databasePath);
        database.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=250");
    }
    auto database = QSqlDatabase::database(m_connectionName, false);
    if (!database.isOpen() && !database.open()) return database.lastError().text();
    return {};
}

void HistoryQueryWorker::query(const HistoryQueryRequest &request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (request.kind < 0 || request.kind > 1 || cancelled(request)) return;

    HistoryQueryResult result;
    result.requestId = request.requestId;
    result.kind = request.kind;
    result.error = openDatabase();
    if (!result.error.isEmpty()) {
        if (!cancelled(request)) emit completed(result);
        return;
    }
    if (cancelled(request)) return;

    const bool telemetry = request.kind == 0;
    const QString timeColumn = telemetry ? QStringLiteral("sample_time") : QStringLiteral("alarm_time");
    QString sql = telemetry
        ? QStringLiteral("SELECT sample_time,device_id,printf('%.1f',temperature),"
                         "printf('%.2f',pressure),rpm,CASE state WHEN 1 THEN '运行' WHEN 2 THEN '故障' "
                         "ELSE '停止' END,fault_code,id FROM telemetry")
        : QStringLiteral("SELECT alarm_time,device_id,alarm_type,message,id FROM alarms");
    sql += QStringLiteral(" WHERE %1>=? AND %1<=?").arg(timeColumn);
    if (request.deviceId > 0) sql += QStringLiteral(" AND device_id=?");
    if (!request.cursor.time.isEmpty())
        sql += QStringLiteral(" AND (%1,id)<(?,?)").arg(timeColumn);
    sql += QStringLiteral(" ORDER BY %1 DESC,id DESC LIMIT %2").arg(timeColumn).arg(PageSize + 1);

    auto database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(sql)) {
        result.error = query.lastError().text();
    } else {
        query.addBindValue(request.start.toString(Qt::ISODateWithMs));
        query.addBindValue(request.end.toString(Qt::ISODateWithMs));
        if (request.deviceId > 0) query.addBindValue(request.deviceId);
        if (!request.cursor.time.isEmpty()) {
            query.addBindValue(request.cursor.time);
            query.addBindValue(request.cursor.id);
        }
        if (!query.exec()) {
            result.error = query.lastError().text();
        } else {
            const int columns = telemetry ? 7 : 4;
            while (query.next()) {
                if (cancelled(request)) return;
                if (result.rows.size() == PageSize) {
                    result.hasMore = true;
                    break;
                }
                QStringList row;
                row.reserve(columns);
                for (int column = 0; column < columns; ++column)
                    row.append(query.value(column).toString());
                result.rows.append(row);
                result.nextCursor = {query.value(0).toString(), query.value(columns).toLongLong()};
            }
            if (query.lastError().isValid()) result.error = query.lastError().text();
        }
    }
    if (!result.error.isEmpty()) {
        result.rows.clear();
        result.hasMore = false;
    }
    if (!cancelled(request)) emit completed(result);
}
