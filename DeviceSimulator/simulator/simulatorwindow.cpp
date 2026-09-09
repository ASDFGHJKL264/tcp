#include "simulatorwindow.h"
#include "simulatorserver.h"

#include <QDateTime>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>

SimulatorWindow::SimulatorWindow(QWidget *parent)
    : QMainWindow(parent), m_server(new SimulatorServer(this))
{
    setWindowTitle(QStringLiteral("TCP工业设备模拟器"));
    resize(650, 420);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *form = new QFormLayout;
    m_port = new QSpinBox;
    m_port->setRange(1024, 65535);
    m_port->setValue(45454);
    m_toggle = new QPushButton(QStringLiteral("停止服务"));
    auto *injectFault = new QPushButton(QStringLiteral("向设备2注入故障"));
    m_clients = new QLabel(QStringLiteral("0"));
    form->addRow(QStringLiteral("监听端口："), m_port);
    form->addRow(QStringLiteral("客户端数量："), m_clients);
    form->addRow(m_toggle);
    form->addRow(injectFault);
    layout->addLayout(form);
    layout->addWidget(new QLabel(QStringLiteral("模拟4台设备，每500ms推送温度、压力、转速和状态。")));
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->document()->setMaximumBlockCount(2000);
    layout->addWidget(m_log);
    setCentralWidget(central);

    connect(m_server, &SimulatorServer::logMessage, this, [this](const QString &message) {
        m_log->append(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + message);
    });
    connect(m_server, &SimulatorServer::clientCountChanged, this,
            [this](int count) { m_clients->setNum(count); });
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (m_server->isListening()) {
            m_server->stop();
            m_toggle->setText(QStringLiteral("启动服务"));
            m_port->setEnabled(true);
        } else if (m_server->start(static_cast<quint16>(m_port->value()))) {
            m_toggle->setText(QStringLiteral("停止服务"));
            m_port->setEnabled(false);
        }
    });
    connect(injectFault, &QPushButton::clicked, this,
            [this] { m_server->injectFault(2, 1001); });
    m_server->start(static_cast<quint16>(m_port->value()));
    m_port->setEnabled(false);
}
