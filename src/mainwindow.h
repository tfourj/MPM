#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMqttClient>
#include <QMqttMessage>
#include <QTimer>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QVector>
#include <QCloseEvent>
#include "actions/actions.h"
#include "common/pc_state.h"

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
    void onMessageReceived(const QMqttMessage &message);
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
    bool m_isControllingService = false;
    QMqttClient::ClientState m_serviceState = QMqttClient::Disconnected;
    bool m_serviceReconnectActive = false;
    bool m_serviceUserInitiated = false;
    int m_startPromptMode = 2; // 0=deny,1=confirm,2=ask
    int m_stopPromptMode = 2;  // 0=deny,1=confirm,2=ask
    bool m_prevServiceAvailable = false;
    bool m_prevServiceLocal = false;
    int m_preferredIpc = 0; // 0=auto,1=local,2=tcp
    int m_serviceMissCount = 0; // consecutive missed status polls
    QTimer *m_servicePollTimer = nullptr;
    void startServicePolling(); // mirror service status and logs into the GUI
    void handOverToService();   // stop the GUI MQTT client and control the service instead

    // MQTT
    QString getSubscribeTopic() const; // mqttpowermanager/%1/+
    QString getAvailabilityTopic() const; // mqttpowermanager/%1/health
    void log(const QString &msg);
    void updateAvailabilityWill();
    void publishAvailabilityOnline();
    void publishAvailabilityOffline();

    // Home Assistant discovery
    bool publishHaDiscovery();
    void subscribeHaStatus();
    void onHaStatusReceived(const QMqttMessage &message);
    void onHaDiscoveryToggled(bool enabled);
    void onHaSyncClicked();
    void onHaDeviceNameEdited();

    // PC state sensor, fed by session and power messages to this window
    PcState::State m_pcState = PcState::State::Unlocked;
    void setPcState(PcState::State state);
    void publishPcState();
    void registerPcStateEvents();
    void unregisterPcStateEvents();

    void loadSettings();
    void saveSettingsIfNeeded();
    void applyUiToClient();
    void updateConnectButton();
    void updateStatusLabel(QMqttClient::ClientState state, QMqttClient::ClientError error = QMqttClient::NoError);
    void scheduleReconnectIfNeeded();
    void saveAllSettingsForce();

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
    void onActionsChanged(); // persist, refresh list and notify the service

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
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void attemptQuitWithServicePrompt();
    bool isServiceInstalled() const;
    bool isServiceRunning() const;
    bool startService();
    bool stopService();
};

#endif // MAINWINDOW_H
