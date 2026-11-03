#include "historyqueryworker.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
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
    if (!request.start.isValid() || !request.end.isValid() || request.start > request.end
        || request.deviceId < 0 || request.deviceId > 4
        || request.start.toUTC().toString(Qt::ISODateWithMs).isEmpty()
        || request.end.toUTC().toString(Qt::ISODateWithMs).isEmpty()) {
        result.error = QStringLiteral("历史查询时间范围或设备编号无效");
        if (!cancelled(request)) emit completed(result);
        return;
    }
    result.error = openDatabase();
    if (!result.error.isEmpty()) {
        if (!cancelled(request)) emit completed(result);
        return;
    }
    if (cancelled(request)) return;

    const bool telemetry = request.kind == 0;
    auto database = QSqlDatabase::database(m_connectionName, false);
    const QString table = telemetry ? QStringLiteral("telemetry") : QStringLiteral("alarms");
    const QString timeColumn = telemetry ? QStringLiteral("sample_time") : QStringLiteral("alarm_time");
    const QString epochColumn = telemetry ? QStringLiteral("sample_epoch_ms") : QStringLiteral("alarm_epoch_ms");
    // Read-only compatibility for old/unupgraded databases. Offsetless strings were
    // written as local time; strings carrying Z or +/-HH:MM already specify an instant.
    const QString explicitZone = "(substr(" + timeColumn + ",-1) IN ('Z','z') OR substr("
        + timeColumn + ",-6,1) IN ('+','-'))";
    const QString legacyTime = "CAST(round((CASE WHEN " + explicitZone
        + " THEN julianday(" + timeColumn + ") ELSE julianday(" + timeColumn
        + ",'utc') END - 2440587.5)*86400000.0) AS INTEGER)";
    QString timeExpression = legacyTime;
    if (database.record(table).indexOf(epochColumn) >= 0) {
        timeExpression = epochColumn;
        // Older writers may still append a row without the new metadata. Detect
        // only that null subset; the normal upgraded path retains its indexed order.
        QSqlQuery pending(database);
        if (!pending.exec("SELECT 1 FROM " + table + " WHERE " + epochColumn
                          + " IS NULL AND " + legacyTime + " IS NOT NULL LIMIT 1")) {
            result.error = pending.lastError().text();
            if (!cancelled(request)) emit completed(result);
            return;
        }
        if (pending.next()) timeExpression = "COALESCE(" + epochColumn + "," + legacyTime + ")";
    }
    QString sql = telemetry
        ? QStringLiteral("SELECT sample_time,device_id,printf('%.1f',temperature),"
                         "printf('%.2f',pressure),rpm,CASE state WHEN 1 THEN '运行' WHEN 2 THEN '故障' "
                         "ELSE '停止' END,fault_code,id,") + timeExpression + " FROM telemetry"
        : QStringLiteral("SELECT alarm_time,device_id,alarm_type,message,id,") + timeExpression + " FROM alarms";
    sql += QStringLiteral(" WHERE %1>=? AND %1<=?").arg(timeExpression);
    if (request.deviceId > 0) sql += QStringLiteral(" AND device_id=?");
    if (!request.cursor.time.isEmpty())
        sql += QStringLiteral(" AND (%1,id)<(?,?)").arg(timeExpression);
    sql += QStringLiteral(" ORDER BY %1 DESC,id DESC LIMIT %2").arg(timeExpression).arg(PageSize + 1);

    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(sql)) {
        result.error = query.lastError().text();
    } else {
        query.addBindValue(request.start.toMSecsSinceEpoch());
        query.addBindValue(request.end.toMSecsSinceEpoch());
        if (request.deviceId > 0) query.addBindValue(request.deviceId);
        if (!request.cursor.time.isEmpty()) {
            query.addBindValue(request.cursor.hasEpoch ? request.cursor.epochMs
                : QDateTime::fromString(request.cursor.time, Qt::ISODateWithMs).toMSecsSinceEpoch());
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
                result.nextCursor = {query.value(0).toString(), query.value(columns).toLongLong(),
                                     query.value(columns + 1).toLongLong(), true};
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
