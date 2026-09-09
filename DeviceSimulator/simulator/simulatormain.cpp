#include "simulatorwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    SimulatorWindow window;
    window.show();
    return app.exec();
}
