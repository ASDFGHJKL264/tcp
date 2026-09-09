#ifndef SIMULATORWINDOW_H
#define SIMULATORWINDOW_H

#include <QMainWindow>

class QLabel;
class QPushButton;
class QSpinBox;
class QTextEdit;
class SimulatorServer;

class SimulatorWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit SimulatorWindow(QWidget *parent = nullptr);

private:
    SimulatorServer *m_server;
    QSpinBox *m_port;
    QPushButton *m_toggle;
    QLabel *m_clients;
    QTextEdit *m_log;
};

#endif
