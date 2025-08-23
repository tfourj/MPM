#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QMessageBox>

void MainWindow::ensureTray()
{
    if (m_trayIcon) return;
    QIcon trayIcon = windowIcon();
    if (trayIcon.isNull()) trayIcon = QIcon::fromTheme("computer");
    m_trayIcon = new QSystemTrayIcon(trayIcon, this);
    m_trayMenu = new QMenu(this);
    m_trayShowHideAction = m_trayMenu->addAction("Hide");
    connect(m_trayShowHideAction, &QAction::triggered, this, [this]() {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            raise();
            activateWindow();
        }
        updateTrayShowHideText();
    });
    m_trayExitAction = m_trayMenu->addAction("Exit");
    connect(m_trayExitAction, &QAction::triggered, this, [this]() {
        attemptQuitWithServicePrompt();
    });
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("MPM");
    m_trayIcon->setVisible(true);
    updateTrayShowHideText();
    updateTrayIconByState();
}

void MainWindow::attemptQuitWithServicePrompt()
{
    if (m_isControllingService) {
        if (m_stopPromptMode == 1) {
            stopService();
        } else if (m_stopPromptMode == 2) {
            if (QMessageBox::question(this, "Stop Service", "Do you want to stop the MPM service as well?") == QMessageBox::Yes) {
                stopService();
            }
        }
    }
    qApp->quit();
}

void MainWindow::updateTrayShowHideText()
{
    if (!m_trayShowHideAction) return;
    m_trayShowHideAction->setText(isVisible() ? "Hide" : "Show");
}

void MainWindow::updateTrayIconByState()
{
    if (!m_trayIcon) return;
    QIcon icon;
    QMqttClient::ClientState effectiveState = m_isControllingService ? m_serviceState : m_client->state();
    if (m_isControllingService && effectiveState == QMqttClient::Disconnected && m_serviceReconnectActive && !m_serviceUserInitiated) {
        effectiveState = QMqttClient::Connecting;
    }
    switch (effectiveState) {
    case QMqttClient::Connected:
        icon = m_trayIconConnected.isNull() ? windowIcon() : m_trayIconConnected;
        break;
    case QMqttClient::Connecting:
        icon = windowIcon();
        break;
    case QMqttClient::Disconnected:
    default:
        icon = m_trayIconDisconnected.isNull() ? windowIcon() : m_trayIconDisconnected;
        break;
    }
    m_trayIcon->setIcon(icon);
}

void MainWindow::onStartMinimizedIfConfigured()
{
    if (m_settings.value("options/startMinimized", false).toBool()) {
        QTimer::singleShot(0, this, [this]() {
            hide();
            updateTrayShowHideText();
        });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        updateTrayShowHideText();
        event->ignore();
        return;
    }
    if (m_isControllingService) {
        if (m_stopPromptMode == 1) {
            stopService();
        } else if (m_stopPromptMode == 2) {
            if (QMessageBox::question(this, "Stop Service", "Do you want to stop the MPM service as well?") == QMessageBox::Yes) {
                stopService();
            }
        }
    }
    QMainWindow::closeEvent(event);
}


