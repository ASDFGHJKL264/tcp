#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "protocol.h"

#include <QHash>
#include <QMainWindow>
#include <QElapsedTimer>

class DatabaseWorker;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QThread;
class QTimer;
class RealTimePlot;
class TcpClientWorker;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &dataDirectory = {});
    ~MainWindow() override;
    void connectConfiguredServer();

signals:
    void requestConnect(const QString &host, quint16 port);
    void requestDisconnect();
    void requestCommand(quint16 deviceId, MonitorProtocol::DeviceCommand command);
    void storeTelemetry(const MonitorProtocol::Telemetry &telemetry);
    void initializeDatabase(const QString &path);

private:
    void buildUi();
    void setupWorkers();
    void loadSettings();
    void saveSettings();
    void handleTelemetry(const MonitorProtocol::Telemetry &telemetry);
    void updateConnectionState(bool connected, const QString &description);
    void appendLog(const QString &message, bool warning = false);
    void sendSelectedCommand(MonitorProtocol::DeviceCommand command);
    void checkOfflineDevices();
    void openHistory();
    QString databasePath() const;
    QString logPath() const;

    QLineEdit *m_host;
    QSpinBox *m_port;
    QPushButton *m_connectButton;
    QLabel *m_connectionState;
    QTableWidget *m_devices;
    QComboBox *m_plotDevice;
    RealTimePlot *m_plot;
    QTextEdit *m_log;
    QThread *m_networkThread;
    QThread *m_databaseThread;
    TcpClientWorker *m_networkWorker;
    DatabaseWorker *m_databaseWorker;
    QTimer *m_offlineTimer;
    QHash<quint16, qint64> m_lastSeen;
    QElapsedTimer m_clock;
    quint64 m_savedSamples = 0;
    quint64 m_failedSamples = 0;
    quint64 m_droppedSamples = 0;
    qint64 m_maxWriteMs = 0;
    bool m_connected = false;
    bool m_sessionActive = false;
    QString m_dataDirectory;
};

#endif
