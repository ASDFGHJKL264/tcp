#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LearningIndustrialQt"));
    QCoreApplication::setApplicationName(QStringLiteral("TcpDeviceMonitor"));
    MainWindow window;
    window.show();
    if (app.arguments().contains(QStringLiteral("--auto-connect")))
        QTimer::singleShot(200, &window, &MainWindow::connectConfiguredServer);
    return app.exec();
}
