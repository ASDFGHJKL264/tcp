#ifndef HISTORYQUERYWORKER_H
#define HISTORYQUERYWORKER_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QStringList>

#include <array>
#include <atomic>
#include <memory>

struct HistoryCursor
{
    QString time;
    qint64 id = 0;
};

struct HistoryQueryRequest
{
    quint64 requestId = 0;
    int kind = 0; // 0: telemetry, 1: alarms
    QDateTime start;
    QDateTime end;
    int deviceId = 0;
    HistoryCursor cursor;
};

struct HistoryQueryResult
{
    quint64 requestId = 0;
    int kind = 0;
    QList<QStringList> rows;
    bool hasMore = false;
    HistoryCursor nextCursor;
    QString error;
};

Q_DECLARE_METATYPE(HistoryQueryRequest)
Q_DECLARE_METATYPE(HistoryQueryResult)

// The GUI can invalidate queued/running work without accessing a SQL connection.
struct HistoryQueryControl
{
    std::array<std::atomic<quint64>, 2> latestRequest {{0, 0}};
    std::atomic_bool stopping {false};
};

class HistoryQueryWorker final : public QObject
{
    Q_OBJECT
public:
    static constexpr int PageSize = 200;
    explicit HistoryQueryWorker(const QString &databasePath,
                                std::shared_ptr<HistoryQueryControl> control,
                                QObject *parent = nullptr);
    ~HistoryQueryWorker() override;

public slots:
    void query(const HistoryQueryRequest &request);

signals:
    void completed(const HistoryQueryResult &result);

private:
    bool cancelled(const HistoryQueryRequest &request) const;
    QString openDatabase();
    QString m_databasePath;
    QString m_connectionName;
    std::shared_ptr<HistoryQueryControl> m_control;
};

#endif
