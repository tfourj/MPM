#include "mainwindow.h"

#include <QApplication>
#include <QSettings>
#include <QSharedMemory>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.setWindowTitle("MPM - MQTT Power Manager");
    
    QSharedMemory sharedMemory("MPM_SharedMemory9825");
    if (!sharedMemory.create(1)) {
        QMessageBox::warning(nullptr, "Already running",
                             "MPM - MQTT Power Manager is already running. \n Please close the existing application before starting a new one.");
        return 0;
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "MPM", "MqttPowerManager");
    const bool startMinimized = settings.value("options/startMinimized", false).toBool();
    if (!startMinimized) {
        w.show();
    }
    return a.exec();
}
