#ifndef ASYNCFILELOGGER_H
#define ASYNCFILELOGGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QThread>

class LogFileWorker;

// GUI-side bounded admission; only the worker accesses the filesystem.
class AsyncFileLogger final : public QObject
{
    Q_OBJECT
public:
    explicit AsyncFileLogger(const QString &path, QObject *parent = nullptr);
    ~AsyncFileLogger() override;
    static constexpr int MaximumPendingLines = 256;
    static constexpr int WarningReserve = 32;
    static constexpr int BatchSize = 32;
    bool append(const QString &line, bool warning = false);
    void shutdown();
    int pendingLines() const { return m_queue.size() + m_inFlight; }
    quint64 droppedNormal() const { return m_droppedNormal; }
    quint64 droppedWarnings() const { return m_droppedWarnings; }
    quint64 writtenLines() const { return m_written; }
    quint64 failedLines() const { return m_failed; }

signals:
    void writeFailed(const QString &reason);
    void overflow(quint64 normal, quint64 warnings);

private:
    struct Entry { QString line; bool warning; };
    void schedule();
    void dispatch();
    QThread m_thread;
    LogFileWorker *m_worker = nullptr;
    QList<Entry> m_queue;
    int m_inFlight = 0;
    bool m_scheduled = false;
    bool m_stopping = false;
    quint64 m_droppedNormal = 0, m_droppedWarnings = 0;
    quint64 m_written = 0, m_failed = 0;
};

#endif
