#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMqttClient>
#include <QTimer>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QVector>
#include <QCloseEvent>
#include "actions/actions.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onConnected();
    void onMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onStateChanged(QMqttClient::ClientState state);
    void onErrorChanged(QMqttClient::ClientError error);
    void onConnectTimeout();
    void onSaveSettingsClicked();

    // Settings tab
    void onStartWithWindowsToggled(bool enabled);
    void onStartMinimizedIfConfigured();
    void onStartupPathLockToggled(bool locked);
    void onBrowseStartupExe();
    void onCreateStartMenuShortcut();

    // Actions list context menu
    void onActionsContextMenu(const QPoint &pos);

private:
    Ui::MainWindow *ui;
    QMqttClient *m_client = nullptr;
    QTimer m_connectTimeoutTimer;
    QTimer m_reconnectTimer;
    QSettings m_settings;
    bool m_userInitiatedDisconnect = false;

    // MQTT
    QString getSubscribeTopic() const; // mqttpowermanager/%1/+
    QString getAvailabilityTopic() const; // mqttpowermanager/%1/health
    void log(const QString &msg);
    void updateAvailabilityWill();
    void publishAvailabilityOnline();
    void publishAvailabilityOffline();

    void loadSettings();
    void saveSettingsIfNeeded();
    void applyUiToClient();
    void updateConnectButton();
    void updateStatusLabel(QMqttClient::ClientState state, QMqttClient::ClientError error = QMqttClient::NoError);
    void scheduleReconnectIfNeeded();

    // Actions persistence and UI
    struct UserActionCfg {
        QString customName;       // Used in MQTT topic suffix
        ActionType type;          // What to run
        QString expectedMessage;  // e.g. PRESS
        QString exePath;          // used when type == OpenExe
    };
    QVector<UserActionCfg> m_actions;
    void loadActions();
    void saveActions();
    void refreshActionsList();
    void updateActionsFooter();

    // Actions tab ui helpers
    void onAddAction();
    void onEditSelectedAction();
    void onDeleteSelectedAction();

    // Tray
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_trayShowHideAction = nullptr;
    QAction *m_trayExitAction = nullptr;
    QIcon m_trayIconConnected;
    QIcon m_trayIconDisconnected;
    void ensureTray();
    void updateTrayShowHideText();
    void updateTrayIconByState();

    // Windows startup
    void updateWindowsStartup(bool enabled);

protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // MAINWINDOW_H
