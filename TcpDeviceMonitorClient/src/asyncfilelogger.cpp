#include "asyncfilelogger.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>
#include <utility>

class LogFileWorker final : public QObject
{
    Q_OBJECT
public:
    explicit LogFileWorker(QString path) : m_path(std::move(path)) {}
    void write(const QStringList &lines)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        int written = 0;
        QString error;
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            emit completed(lines.size(), 0, QStringLiteral("无法创建日志目录"));
            return;
        }
        QFile file(m_path);
        for (const auto &line : lines) {
            const QByteArray bytes = line.toUtf8() + '\n';
            if (!file.isOpen() && !file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                error = file.errorString();
                break;
            }
            if (file.size() + bytes.size() > 5 * 1024 * 1024) {
                file.close();
                bool rotated = !QFile::exists(m_path + ".9") || QFile::remove(m_path + ".9");
                for (int n = 8; rotated && n >= 1; --n) {
                    const QString from = m_path + "." + QString::number(n);
                    if (QFile::exists(from))
                        rotated = QFile::rename(from, m_path + "." + QString::number(n + 1));
                }
                if (rotated) rotated = QFile::rename(m_path, m_path + ".1");
                if (!rotated || !file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                    error = QStringLiteral("日志轮转或重新打开失败，停止本批写入");
                    break;
                }
            }
            if (file.write(bytes) != bytes.size()) {
                error = file.errorString();
                break;
            }
            ++written;
        }
        // A flush failure makes this batch unsuccessful; it is not durable storage.
        if (file.isOpen() && !file.flush()) {
            written = 0;
            error = file.errorString();
        }
        emit completed(lines.size(), written, error);
    }
signals:
    void completed(int submitted, int written, const QString &error);
private:
    QString m_path;
};

AsyncFileLogger::AsyncFileLogger(const QString &path, QObject *parent)
    : QObject(parent), m_worker(new LogFileWorker(path))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &LogFileWorker::completed, this,
            [this](int submitted, int written, const QString &error) {
        m_inFlight = qMax(0, m_inFlight - submitted);
        m_written += written;
        m_failed += submitted - written;
        if (!error.isEmpty()) emit writeFailed(error);
        schedule();
    });
    m_thread.setObjectName(QStringLiteral("TcpFileLogThread"));
    m_thread.start();
}

AsyncFileLogger::~AsyncFileLogger() { shutdown(); }

bool AsyncFileLogger::append(const QString &line, bool warning)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return false;
    const auto drop = [this](bool important) {
        important ? ++m_droppedWarnings : ++m_droppedNormal;
        if (m_droppedNormal + m_droppedWarnings == 1 ||
            (m_droppedNormal + m_droppedWarnings) % 100 == 0 || important)
            emit overflow(m_droppedNormal, m_droppedWarnings);
    };
    if (!warning && pendingLines() >= MaximumPendingLines - WarningReserve) {
        drop(false);
        return false;
    }
    if (pendingLines() >= MaximumPendingLines) {
        auto it = std::find_if(m_queue.begin(), m_queue.end(), [](const Entry &entry) {
            return !entry.warning;
        });
        if (it == m_queue.end()) {
            drop(warning);
            return false;
        }
        m_queue.erase(it);
        drop(false);
    }
    QString bounded = line.left(4096);
    bounded.replace('\r', ' ');
    bounded.replace('\n', ' ');
    if (line.size() > 4096) bounded += QStringLiteral(" [日志已截断]");
    m_queue.append({bounded, warning});
    schedule();
    return true;
}

void AsyncFileLogger::schedule()
{
    if (m_stopping || m_scheduled || m_inFlight || m_queue.isEmpty()) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, &AsyncFileLogger::dispatch);
}

void AsyncFileLogger::dispatch()
{
    m_scheduled = false;
    if (m_stopping || m_inFlight || m_queue.isEmpty()) return;
    QStringList batch;
    while (!m_queue.isEmpty() && batch.size() < BatchSize)
        batch.append(m_queue.takeFirst().line);
    m_inFlight += batch.size();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, batch] { worker->write(batch); },
                              Qt::QueuedConnection);
}

void AsyncFileLogger::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return;
    m_stopping = true;
    QStringList remaining;
    while (!m_queue.isEmpty()) remaining.append(m_queue.takeFirst().line);
    m_inFlight += remaining.size();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, remaining] {
        if (!remaining.isEmpty()) worker->write(remaining);
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
    // The worker does not wait for GUI callbacks. Already dispatched work precedes this.
    m_thread.wait();
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    m_worker = nullptr;
}

#include "asyncfilelogger.moc"
