#include "mainwindow.h"
#include "asyncfilelogger.h"
#include "databaseworker.h"
#include "historydialog.h"
#include "realtimeplot.h"
#include "tcpclientworker.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

using namespace MonitorProtocol;

MainWindow::MainWindow(QWidget *parent, const QString &dataDirectory)
    : QMainWindow(parent), m_dataDirectory(dataDirectory)
{
    qRegisterMetaType<Telemetry>();
    m_clock.start();
    qRegisterMetaType<DeviceCommand>();
    buildUi();
    m_fileLogger = new AsyncFileLogger(logPath(), this);
    connect(m_fileLogger, &AsyncFileLogger::writeFailed, this, [this](const QString &reason) {
        m_log->append(QStringLiteral("[日志写盘失败] %1").arg(reason.toHtmlEscaped()));
    });
    connect(m_fileLogger, &AsyncFileLogger::overflow, this, [this](quint64 normal, quint64 warnings) {
        m_log->append(QStringLiteral("[日志队列过载] 普通日志丢弃%1，警告日志丢弃%2；磁盘日志可能不完整")
                      .arg(normal).arg(warnings));
    });
    loadSettings();
    setupWorkers();
    m_offlineTimer = new QTimer(this);
    m_offlineTimer->setInterval(1000);
    connect(m_offlineTimer, &QTimer::timeout, this, &MainWindow::checkOfflineDevices);
    m_offlineTimer->start();
    appendLog(QStringLiteral("系统启动完成，请先运行设备模拟器再建立连接"));
}

MainWindow::~MainWindow()
{
    saveSettings();
    m_offlineTimer->stop();
    NetworkUiUpdate remainingUi;
    if (m_networkThread->isRunning()) {
        QMetaObject::invokeMethod(m_networkWorker, [worker = m_networkWorker, &remainingUi] {
            worker->shutdown();
            remainingUi = worker->takeFinalUiUpdate();
            QThread::currentThread()->quit();
        }, Qt::QueuedConnection);
        m_networkThread->wait(); // Never destroy a still-running QThread.
    }
    // The network producer has stopped. Deliver its bounded remaining samples
    // before queueing database shutdown, so accepted telemetry is drained.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    // At most 128 finite waiting logs remain. The producer never waits for GUI
    // acknowledgements during shutdown; its older queued batch is drained first.
    handleNetworkUiUpdate(remainingUi);
    if (m_databaseThread->isRunning()) {
        QMetaObject::invokeMethod(m_databaseWorker, [worker = m_databaseWorker] {
            worker->shutdown();
            QThread::currentThread()->quit();
        }, Qt::QueuedConnection);
        m_databaseThread->wait();
    }
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    appendLog(QStringLiteral("存储统计：成功 %1，失败 %2，过载丢弃 %3，最慢写入 %4ms")
        .arg(m_savedSamples).arg(m_failedSamples).arg(m_droppedSamples).arg(m_maxWriteMs));
    m_fileLogger->shutdown();
}

void MainWindow::connectConfiguredServer()
{
    emit requestConnect(m_host->text(), static_cast<quint16>(m_port->value()));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Qt多线程TCP工业设备监控系统"));
    resize(1180, 760);
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    auto *connectionBox = new QGroupBox(QStringLiteral("服务器连接"));
    auto *connectionLayout = new QHBoxLayout(connectionBox);
    m_host = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_port = new QSpinBox;
    m_port->setRange(1024, 65535);
    m_port->setValue(45454);
    m_connectButton = new QPushButton(QStringLiteral("建立连接"));
    m_connectionState = new QLabel(QStringLiteral("未连接"));
    m_connectionState->setStyleSheet("font-weight:bold;color:#c62828;");
    connectionLayout->addWidget(new QLabel(QStringLiteral("服务器：")));
    connectionLayout->addWidget(m_host, 1);
    connectionLayout->addWidget(new QLabel(QStringLiteral("端口：")));
    connectionLayout->addWidget(m_port);
    connectionLayout->addWidget(m_connectButton);
    connectionLayout->addWidget(m_connectionState);
    root->addWidget(connectionBox);

    m_devices = new QTableWidget(4, 8);
    m_devices->setHorizontalHeaderLabels({QStringLiteral("设备ID"), QStringLiteral("在线状态"),
        QStringLiteral("运行状态"), QStringLiteral("温度(℃)"), QStringLiteral("压力(MPa)"),
        QStringLiteral("转速(rpm)"), QStringLiteral("故障码"), QStringLiteral("更新时间")});
    m_devices->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_devices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_devices->setSelectionMode(QAbstractItemView::SingleSelection);
    m_devices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < 4; ++row) {
        m_devices->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        for (int column = 1; column < 8; ++column)
            m_devices->setItem(row, column, new QTableWidgetItem(column == 1 ? QStringLiteral("离线") : "--"));
    }
    m_devices->selectRow(0);
    root->addWidget(m_devices);

    auto *commandLayout = new QHBoxLayout;
    auto *start = new QPushButton(QStringLiteral("启动设备"));
    auto *stop = new QPushButton(QStringLiteral("停止设备"));
    auto *reset = new QPushButton(QStringLiteral("故障复位"));
    auto *history = new QPushButton(QStringLiteral("历史数据/报警记录"));
    commandLayout->addWidget(start);
    commandLayout->addWidget(stop);
    commandLayout->addWidget(reset);
    commandLayout->addStretch();
    commandLayout->addWidget(history);
    root->addLayout(commandLayout);

    auto *plotBox = new QGroupBox(QStringLiteral("实时趋势（最多保留300点）"));
    auto *plotLayout = new QVBoxLayout(plotBox);
    m_plotDevice = new QComboBox;
    for (int id = 1; id <= 4; ++id)
        m_plotDevice->addItem(QStringLiteral("设备%1").arg(id), id);
    m_plot = new RealTimePlot;
    plotLayout->addWidget(m_plotDevice, 0, Qt::AlignLeft);
    plotLayout->addWidget(m_plot);
    root->addWidget(plotBox, 1);

    auto *logBox = new QGroupBox(QStringLiteral("运行日志"));
    auto *logLayout = new QVBoxLayout(logBox);
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->document()->setMaximumBlockCount(2000);
    m_log->setMaximumHeight(130);
    logLayout->addWidget(m_log);
    root->addWidget(logBox);
    setCentralWidget(central);

    connect(m_connectButton, &QPushButton::clicked, this, [this] {
        if (m_sessionActive)
            emit requestDisconnect();
        else {
            saveSettings();
            emit requestConnect(m_host->text(), static_cast<quint16>(m_port->value()));
        }
    });
    connect(start, &QPushButton::clicked, this, [this] { sendSelectedCommand(DeviceCommand::Start); });
    connect(stop, &QPushButton::clicked, this, [this] { sendSelectedCommand(DeviceCommand::Stop); });
    connect(reset, &QPushButton::clicked, this, [this] { sendSelectedCommand(DeviceCommand::Reset); });
    connect(history, &QPushButton::clicked, this, &MainWindow::openHistory);
    connect(m_plotDevice, &QComboBox::currentIndexChanged, this, [this] {
        m_plot->selectDevice(m_plotDevice->currentData().toInt());
    });
    menuBar()->addAction(QStringLiteral("历史数据"), this, &MainWindow::openHistory);
}

void MainWindow::setupWorkers()
{
    m_networkThread = new QThread(this);
    m_databaseThread = new QThread(this);
    m_networkWorker = new TcpClientWorker;
    m_databaseWorker = new DatabaseWorker;
    m_networkWorker->moveToThread(m_networkThread);
    m_databaseWorker->moveToThread(m_databaseThread);
    connect(m_networkThread, &QThread::finished, m_networkWorker, &QObject::deleteLater);
    connect(m_databaseThread, &QThread::finished, m_databaseWorker, &QObject::deleteLater);
    connect(m_networkThread, &QThread::started, m_networkWorker, &TcpClientWorker::initialize);
    connect(this, &MainWindow::requestConnect, m_networkWorker, &TcpClientWorker::connectToServer);
    connect(this, &MainWindow::requestDisconnect, m_networkWorker, &TcpClientWorker::disconnectFromServer);
    connect(this, &MainWindow::requestCommand, m_networkWorker, &TcpClientWorker::sendCommand);
    connect(m_networkWorker, &TcpClientWorker::telemetryReceived, this, &MainWindow::handleTelemetry);
    connect(m_networkWorker, &TcpClientWorker::connectionStateChanged,
            this, &MainWindow::updateConnectionState);
    connect(m_networkWorker, &TcpClientWorker::uiUpdateReady,
            this, &MainWindow::handleNetworkUiUpdate);
    connect(this, &MainWindow::networkUiUpdateProcessed,
            m_networkWorker, &TcpClientWorker::uiUpdateProcessed);
    connect(this, &MainWindow::storedTelemetryProcessed,
            m_networkWorker, &TcpClientWorker::telemetryProcessed);
    connect(m_networkWorker, &TcpClientWorker::sessionActiveChanged, this, [this](bool active) {
        m_sessionActive = active;
        m_host->setEnabled(!active);
        m_port->setEnabled(!active);
        m_connectButton->setText(active ? QStringLiteral("取消/断开连接") : QStringLiteral("建立连接"));
    });
    connect(m_networkWorker, &TcpClientWorker::commandFinished, this,
            [this](quint16 id, quint32 sequence, bool ok, const QString &reason) {
                appendLog(QStringLiteral("设备%1命令#%2：%3").arg(id).arg(sequence).arg(reason), !ok);
            });

    connect(this, &MainWindow::initializeDatabase, m_databaseWorker, &DatabaseWorker::initialize);
    connect(this, &MainWindow::storeTelemetry, m_databaseWorker, &DatabaseWorker::storeTelemetry);
    connect(m_databaseWorker, &DatabaseWorker::sampleProcessed, this,
            [this](bool saved, qint64 elapsedMs) {
                saved ? ++m_savedSamples : ++m_failedSamples;
                m_maxWriteMs = qMax(m_maxWriteMs, elapsedMs);
                // Return the credit only after GUI consumes the completion.
                // Otherwise a fast failing database can continuously replenish
                // credits while its error/completion GUI MetaCalls accumulate.
                emit storedTelemetryProcessed();
            });
    connect(m_databaseWorker, &DatabaseWorker::databaseError, this,
            [this](const QString &error) { appendLog(QStringLiteral("数据库错误：%1").arg(error), true); });
    connect(m_databaseWorker, &DatabaseWorker::alarmRaised, this,
            [this](const QString &alarm) { appendLog(QStringLiteral("【报警】%1").arg(alarm), true); });
    connect(m_databaseWorker, &DatabaseWorker::ready, this,
            [this](const QString &) { appendLog(QStringLiteral("SQLite异步存储已启用")); });
    m_networkThread->start();
    m_databaseThread->start();
    emit initializeDatabase(databasePath());
}

void MainWindow::handleTelemetry(const Telemetry &t)
{
    if (t.deviceId < 1 || t.deviceId > 4)
        return;
    const int row = t.deviceId - 1;
    const QStringList values = {stateText(t.state),
        QString::number(t.temperature, 'f', 1), QString::number(t.pressure, 'f', 2),
        QString::number(t.rpm), QString::number(t.faultCode), t.timestamp.toString("HH:mm:ss.zzz")};
    for (int i = 0; i < values.size(); ++i)
        m_devices->item(row, i + 2)->setText(values.at(i));
    const QColor color = t.state == DeviceState::Fault ? QColor(198, 40, 40) : QColor(46, 125, 50);
    m_devices->item(row, 2)->setForeground(color);
    m_plot->appendPoint(t.deviceId, t.temperature, t.pressure);
    emit storeTelemetry(t);
}

void MainWindow::handleNetworkUiUpdate(const NetworkUiUpdate &update)
{
    if (m_connected) {
        for (quint16 id = 1; id <= 4; ++id) {
            const qint64 receiptTime = update.lastTelemetry[id - 1];
            if (receiptTime >= 0)
                m_lastSeen[id] = receiptTime;
            else
                m_lastSeen.remove(id);
            m_storageLimited[id] = update.storageLimited[id - 1];
        }
    }
    for (const auto &entry : update.logs)
        appendLog(entry.message, entry.warning);
    if (update.droppedNormalLogs != m_droppedNetworkNormalLogs
        || update.droppedWarningLogs != m_droppedNetworkWarningLogs) {
        m_droppedNetworkNormalLogs = update.droppedNormalLogs;
        m_droppedNetworkWarningLogs = update.droppedWarningLogs;
        appendLog(QStringLiteral("网络日志入口过载：普通日志累计丢弃 %1，警告日志累计丢弃 %2；继续接收通信，日志可能不完整")
            .arg(m_droppedNetworkNormalLogs).arg(m_droppedNetworkWarningLogs), true);
    }
    if (update.droppedTelemetry != m_droppedSamples) {
        m_droppedSamples = update.droppedTelemetry;
        appendLog(QStringLiteral("存储链路过载：累计丢弃 %1 条遥测，历史及报警可能缺失；设备在线状态仍按合法遥测接收判断")
            .arg(m_droppedSamples), true);
    }
    statusBar()->showMessage(QStringLiteral("待存储 %1/64 · 峰值 %2 · 已保存 %3 · 写入失败 %4 · 过载丢弃 %5 · 最慢写入 %6ms")
        .arg(update.pendingTelemetry).arg(update.peakTelemetry).arg(m_savedSamples)
        .arg(m_failedSamples).arg(update.droppedTelemetry).arg(m_maxWriteMs));
    checkOfflineDevices();
    // A signal connection is automatically removed when the worker is deleted;
    // the finite shutdown drain never dereferences its former raw pointer.
    emit networkUiUpdateProcessed();
}

void MainWindow::updateConnectionState(bool connected, const QString &description)
{
    m_connected = connected;
    m_connectButton->setText(m_sessionActive ? QStringLiteral("取消/断开连接") : QStringLiteral("建立连接"));
    if (!connected) {
        m_plot->clear();
        m_lastSeen.clear();
        m_storageLimited.clear();
        checkOfflineDevices();
    }
    m_connectionState->setText(description);
    m_connectionState->setStyleSheet(QString("font-weight:bold;color:%1;")
                                     .arg(connected ? "#2e7d32" : "#c62828"));
}

void MainWindow::sendSelectedCommand(DeviceCommand command)
{
    const int row = m_devices->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("选择设备"), QStringLiteral("请先选择一台设备。"));
        return;
    }
    emit requestCommand(static_cast<quint16>(row + 1), command);
}

void MainWindow::checkOfflineDevices()
{
    const qint64 now = m_clock.msecsSinceReference() + m_clock.elapsed();
    for (quint16 id = 1; id <= 4; ++id) {
        if (!m_connected || !m_lastSeen.contains(id) || now - m_lastSeen.value(id) > 3000) {
            m_devices->item(id - 1, 1)->setText(QStringLiteral("离线"));
            m_devices->item(id - 1, 1)->setToolTip(QStringLiteral("超过3秒未收到合法遥测，或TCP连接已断开"));
            m_devices->item(id - 1, 1)->setForeground(QColor(198, 40, 40));
            for (int column = 2; column < 8; ++column)
                m_devices->item(id - 1, column)->setText("--");
        } else {
            const bool limited = m_storageLimited.value(id);
            m_devices->item(id - 1, 1)->setText(limited
                ? QStringLiteral("在线（存储过载）") : QStringLiteral("在线"));
            m_devices->item(id - 1, 1)->setForeground(limited
                ? QColor(230, 120, 0) : QColor(46, 125, 50));
            m_devices->item(id - 1, 1)->setToolTip(limited
                ? QStringLiteral("通信仍在线；当前数值是最近已交付样本，存储过载时实时及历史数据可能缺失")
                : QStringLiteral("最近3秒内收到合法遥测；通信状态不依赖数据库写入额度"));
        }
    }
}

void MainWindow::appendLog(const QString &message, bool warning)
{
    const QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    const QString line = QStringLiteral("[%1] %2").arg(time, message);
    m_log->append(warning ? QStringLiteral("<font color='#c62828'>%1</font>").arg(line.toHtmlEscaped())
                          : line.toHtmlEscaped());
    m_fileLogger->append(line, warning);
}

QString MainWindow::databasePath() const
{
    const QString directory = m_dataDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : m_dataDirectory;
    return QDir(directory).filePath("data/monitor.db");
}

QString MainWindow::logPath() const
{
    const QString directory = m_dataDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : m_dataDirectory;
    return QDir(directory).filePath("logs/monitor.log");
}

void MainWindow::openHistory()
{
    auto *dialog = new HistoryDialog(databasePath(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::loadSettings()
{
    QSettings settings;
    m_host->setText(settings.value("network/host", "127.0.0.1").toString());
    m_port->setValue(settings.value("network/port", 45454).toInt());
    restoreGeometry(settings.value("window/geometry").toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("network/host", m_host->text());
    settings.setValue("network/port", m_port->value());
    settings.setValue("window/geometry", saveGeometry());
}
