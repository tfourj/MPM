#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QProcess>
#include <QDebug>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <algorithm>
#include <QFileDialog>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QtGlobal>
#include <QThread>
#include <QSettings>
#include "common/settings.h"
#include "common/service_ipc_client.h"
#include "common/crypto_win.h"
#include <QSystemTrayIcon>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSessionManager>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_settings(mpmSharedSettingsFilePath(), QSettings::IniFormat)
{
    ui->setupUi(this);

    // Base app icon from resources
    setWindowIcon(QIcon(":/assets/mpm_white.png"));

    // Build green/red tray icons from base asset
    auto makeTintedIcon = [](const QString &resPath, const QColor &color) -> QIcon {
        QPixmap base(resPath);
        if (base.isNull()) return QIcon();
        QPixmap tinted = base;
        tinted.fill(Qt::transparent);
        {
            QPainter p(&tinted);
            p.drawPixmap(0, 0, base);
            p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            p.fillRect(tinted.rect(), color);
            p.end();
        }
        return QIcon(tinted);
    };
    m_trayIconConnected = makeTintedIcon(":/assets/mpm_white.png", QColor(0, 170, 0));
    m_trayIconDisconnected = makeTintedIcon(":/assets/mpm_white.png", QColor(200, 0, 0));

    // UI signals
    connect(ui->pushButtonConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    // Actions tab
    connect(ui->listWidgetActions, &QListWidget::currentRowChanged, this, [this](int){ updateActionsFooter(); });
    connect(ui->pushButtonAddAction, &QPushButton::clicked, this, &MainWindow::onAddAction);
    connect(ui->pushButtonEditAction, &QPushButton::clicked, this, &MainWindow::onEditSelectedAction);
    connect(ui->pushButtonDeleteAction, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedAction);
    ui->listWidgetActions->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidgetActions, &QListWidget::customContextMenuRequested, this, &MainWindow::onActionsContextMenu);

    // Add MQTT client
    m_client = new QMqttClient(this);

    // Default values if not configured yet
    m_client->setHostname("127.0.0.1");
    m_client->setPort(1883);
    m_client->setClientId("QtMqttClientPC");

    connect(m_client, &QMqttClient::connected, this, &MainWindow::onConnected);
    connect(m_client, &QMqttClient::messageReceived, this, &MainWindow::onMessageReceived);
    connect(m_client, &QMqttClient::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_client, &QMqttClient::errorChanged, this, &MainWindow::onErrorChanged);

    // Connect timeout timer
    m_connectTimeoutTimer.setSingleShot(true);
    connect(&m_connectTimeoutTimer, &QTimer::timeout, this, &MainWindow::onConnectTimeout);

    // Reconnect timer
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (ui->checkBoxAutoReconnect->isChecked()) {
            log("Auto-reconnect: attempting to connect...");
            onConnectClicked();
        }
    });

    loadSettings();

    connect(ui->checkBoxStartWithWindows, &QCheckBox::toggled, this, &MainWindow::onStartWithWindowsToggled);
    connect(ui->checkBoxAutoConnect, &QCheckBox::toggled, this, [this](bool){ saveAllSettingsForce(); });
    connect(ui->checkBoxAutoReconnect, &QCheckBox::toggled, this, [this](bool){ saveAllSettingsForce(); });
    connect(ui->spinBoxReconnectSec, qOverload<int>(&QSpinBox::valueChanged), this, [this](int){ saveAllSettingsForce(); });
    connect(ui->checkBoxStartMinimized, &QCheckBox::toggled, this, [this](bool){ saveAllSettingsForce(); });
    // Startup path lock UI
    connect(ui->checkBoxLockStartupPath, &QCheckBox::toggled, this, &MainWindow::onStartupPathLockToggled);
    connect(ui->pushButtonBrowseStartupPath, &QPushButton::clicked, this, &MainWindow::onBrowseStartupExe);
    connect(ui->lineEditStartupPath, &QLineEdit::editingFinished, this, [this]() {
        saveAllSettingsForce();
        if (ui->checkBoxStartWithWindows->isChecked()) updateWindowsStartup(true);
    });
    connect(ui->pushButtonCreateStartMenuShortcut, &QPushButton::clicked, this, &MainWindow::onCreateStartMenuShortcut);
    loadActions();
    applyUiToClient();
    updateConnectButton();
    updateStatusLabel(m_client->state());
    updateTrayIconByState();

    // Load service-only preference
    const bool serviceOnly = m_settings.value("service/useOnly", false).toBool();
    if (ui->checkBoxServiceUseOnly) ui->checkBoxServiceUseOnly->setChecked(serviceOnly);
    // If service IPC is available or service-only is enabled, control service instead of local client
    const bool serviceAvailable = ServiceIpcClient::isAvailable();
    if (serviceAvailable || serviceOnly) {
        log("Service detected: GUI will control service connection");
        m_isControllingService = true;
        // Apply preferred IPC; small wait for preferred transport
        bool localNow = false;
        if (m_preferredIpc == 1) {
            for (int i = 0; i < 5; ++i) { if (ServiceIpcClient::isAvailableLocal()) { localNow = true; break; } QThread::msleep(100); }
        } else if (m_preferredIpc == 2) {
            localNow = false;
        } else {
            for (int i = 0; i < 3; ++i) { if (ServiceIpcClient::isAvailableLocal()) { localNow = true; break; } QThread::msleep(100); }
        }
        m_prevServiceLocal = true;
        const QString srcText = QString("Source: Service (Local)");
        if (ui->labelConnSource) ui->labelConnSource->setText(srcText);
        log(QString("IPC transport: Local"));
        // Mirror service status into GUI periodically
        QTimer *poll = new QTimer(this);
        poll->setInterval(1000);
        connect(poll, &QTimer::timeout, this, [this]() {
            // Reflect current transport by the last used sender path
            // Try to query service every tick; if it fails repeatedly, UI will reflect via miss counter below
            QByteArray resp = ServiceIpcClient::sendPreferred(m_preferredIpc, QByteArrayLiteral("status2"), QStringLiteral("MPMServiceIpc"), 200);
            if (resp.isEmpty()) {
                resp = ServiceIpcClient::sendPreferred(m_preferredIpc, QByteArrayLiteral("status"), QStringLiteral("MPMServiceIpc"), 200);
            }
            if (!resp.isEmpty()) {
                m_serviceMissCount = 0;
                bool ok = false;
                QMqttClient::ClientState rawState = QMqttClient::Disconnected;
                bool recoActive = false;
                bool userDisc = false;
                const QString s = QString::fromUtf8(resp);
                const QStringList parts = s.split(',');
                if (parts.size() >= 1) {
                    int v = parts[0].toInt(&ok);
                    if (ok) rawState = static_cast<QMqttClient::ClientState>(v);
                }
                if (parts.size() >= 2) {
                    recoActive = (parts[1].trimmed() == QLatin1String("1"));
                }
                if (parts.size() >= 4) {
                    userDisc = (parts[3].trimmed() == QLatin1String("1"));
                }
                // Fallback when only simple status returned
                m_serviceState = rawState;
                m_serviceReconnectActive = recoActive;
                m_serviceUserInitiated = userDisc;

                // Compute effective state for UI: show Connecting when auto-reconnect loop is active
                QMqttClient::ClientState effectiveState = m_serviceState;
                if (m_serviceState == QMqttClient::Disconnected && m_serviceReconnectActive && !m_serviceUserInitiated) {
                    effectiveState = QMqttClient::Connecting;
                }

                if (true) {
                    updateStatusLabel(effectiveState);
                    updateTrayIconByState();
                    if (ui->labelConnSource) ui->labelConnSource->setText("Source: Service (Local)");
                    if (ui && ui->pushButtonConnect) {
                        switch (effectiveState) {
                        case QMqttClient::Connected: ui->pushButtonConnect->setText("Disconnect"); break;
                        case QMqttClient::Connecting: ui->pushButtonConnect->setText("Connecting.."); break;
                        case QMqttClient::Disconnected: default: ui->pushButtonConnect->setText("Connect"); break;
                        }
                    }
                }
            } else {
                // Missed status response
                m_serviceMissCount = qMin(m_serviceMissCount + 1, 10);
                if (m_serviceMissCount >= 3) {
                    if (ui->labelConnSource) ui->labelConnSource->setText("Source: Service (Unavailable)");
                    updateStatusLabel(QMqttClient::Disconnected);
                    updateTrayIconByState();
                    if (ui && ui->pushButtonConnect) ui->pushButtonConnect->setText("Connect");
                }
            }
            static int tick = 0; tick = (tick + 1) % 3;
            if (tick == 0) {
                const QByteArray logs = ServiceIpcClient::sendPreferred(m_preferredIpc, QByteArrayLiteral("getlogs"), QStringLiteral("MPMServiceIpc"), 200);
                if (!logs.isEmpty()) {
                    const QString s = QString::fromUtf8(logs);
                    if (!s.trimmed().isEmpty()) ui->textEditLog->append(s.trimmed());
                }
            }
        });
        poll->start();
        // If user forces service-only but service isn't available, avoid lag: disable connect
        if (serviceOnly && !serviceAvailable && ui && ui->pushButtonConnect) {
            ui->pushButtonConnect->setEnabled(false);
            ui->pushButtonConnect->setToolTip("Service-only mode enabled but service is not available");
        }
    }
    // If service-only is enabled and service becomes available later, re-enable Connect and switch source label
    QTimer *svcAvailPoll = new QTimer(this);
    svcAvailPoll->setInterval(1000);
    connect(svcAvailPoll, &QTimer::timeout, this, [this]() {
        const int lt = ServiceIpcClient::lastTransport();
        const bool local = (lt == 1) ? true : ServiceIpcClient::isAvailableLocal();
        if (ui && ui->checkBoxServiceUseOnly && ui->checkBoxServiceUseOnly->isChecked()) {
            if (local) {
                if (ui->pushButtonConnect && !ui->pushButtonConnect->isEnabled()) {
                    ui->pushButtonConnect->setEnabled(true);
                    ui->pushButtonConnect->setToolTip("");
                    log(QString("Service became available via Local"));
                }
                if (ui->labelConnSource) ui->labelConnSource->setText("Source: Service (Local)");
                m_isControllingService = true;
            }
        }
    });
    svcAvailPoll->start();

    ensureTray();
    // Ensure registry startup reflects setting
    updateWindowsStartup(ui->checkBoxStartWithWindows->isChecked());
    onStartMinimizedIfConfigured();

    // Set version label in footer
#ifdef APP_VERSION
    if (ui->labelVersion) {
        ui->labelVersion->setText(QString("v%1").arg(QString::fromUtf8(APP_VERSION)));
    }
#endif

    if (m_settings.value("options/autoConnect", false).toBool()) {
        // If service-only is enabled, don't auto-connect the GUI client to avoid lag and respect preference
        if (!(ui->checkBoxServiceUseOnly && ui->checkBoxServiceUseOnly->isChecked())) {
            QTimer::singleShot(0, this, [this]() { onConnectClicked(); });
        }
    }

    // Service prompt modes & IPC prefs
    m_startPromptMode = m_settings.value("service/startPromptMode", 2).toInt();
    m_stopPromptMode = m_settings.value("service/stopPromptMode", 2).toInt();
    if (ui->comboBoxStartMode) ui->comboBoxStartMode->setCurrentIndex(qBound(0, m_startPromptMode, 2));
    if (ui->comboBoxStopMode) ui->comboBoxStopMode->setCurrentIndex(qBound(0, m_stopPromptMode, 2));
    m_preferredIpc = 1; // Local only
    if (isServiceInstalled() && !isServiceRunning()) {
        // 0=deny: do nothing
        // 1=confirm: start without asking
        // 2=ask: show prompt
        if (m_startPromptMode == 1) {
            if (!startService()) {
                QMessageBox::warning(this, "Service", "Failed to start service. Please run as Administrator.");
            }
        } else if (m_startPromptMode == 2) {
            if (QMessageBox::question(this, "Start Service", "MPM service is installed but not running. Start it now?") == QMessageBox::Yes) {
                if (!startService()) {
                    QMessageBox::warning(this, "Service", "Failed to start service. Please run as Administrator.");
                }
            }
        }
    }

    // Ensure we publish offline on app exit (graceful)
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        publishAvailabilityOffline();
    });

    // On OS logout/shutdown or session end, optionally stop the service
    connect(qApp, &QGuiApplication::commitDataRequest, this, [this](QSessionManager &){
        if (m_isControllingService) {
            if (m_stopPromptMode == 1) {
                stopService();
            } else if (m_stopPromptMode == 2) {
                if (QMessageBox::question(this, "Stop Service", "Do you want to stop the MPM service as well?") == QMessageBox::Yes) {
                    stopService();
                }
            }
        }
    });

    // Wire save settings button
    connect(ui->pushButtonSaveSettings, &QPushButton::clicked, this, &MainWindow::onSaveSettingsClicked);

    // Service control buttons
    if (ui->pushButtonServiceStart) {
        connect(ui->pushButtonServiceStart, &QPushButton::clicked, this, [this]() {
            if (!isServiceInstalled()) { QMessageBox::warning(this, "Service", "Service is not installed."); return; }
            if (startService()) log("Service start requested"); else QMessageBox::warning(this, "Service", "Failed to start service.");
        });
    }
    if (ui->pushButtonServiceStop) {
        connect(ui->pushButtonServiceStop, &QPushButton::clicked, this, [this]() {
            if (!isServiceInstalled()) { QMessageBox::warning(this, "Service", "Service is not installed."); return; }
            if (stopService()) log("Service stop requested"); else QMessageBox::warning(this, "Service", "Failed to stop service.");
        });
    }
    if (ui->pushButtonServiceCheck) {
        connect(ui->pushButtonServiceCheck, &QPushButton::clicked, this, [this]() {
            if (!isServiceInstalled()) { QMessageBox::information(this, "Service", "Service is not installed."); return; }
            QMessageBox::information(this, "Service", isServiceRunning() ? "Service is running" : "Service is stopped");
        });
    }
    if (ui->comboBoxStartMode) {
        connect(ui->comboBoxStartMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx){
            m_startPromptMode = qBound(0, idx, 2);
            m_settings.setValue("service/startPromptMode", m_startPromptMode);
        });
    }
    if (ui->comboBoxStopMode) {
        connect(ui->comboBoxStopMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx){
            m_stopPromptMode = qBound(0, idx, 2);
            m_settings.setValue("service/stopPromptMode", m_stopPromptMode);
        });
    }
    // TCP-related UI elements removed; default to Local-only
    if (ui->checkBoxServiceUseOnly) {
        connect(ui->checkBoxServiceUseOnly, &QCheckBox::toggled, this, [this](bool on){
            m_settings.setValue("service/useOnly", on);
            if (on) {
                m_isControllingService = true;
                log("Service-only mode enabled: GUI MQTT disabled");
                // Disable GUI connect if service not available to prevent lag
                if (!ServiceIpcClient::isAvailable() && ui && ui->pushButtonConnect) {
                    ui->pushButtonConnect->setEnabled(false);
                    ui->pushButtonConnect->setToolTip("Service-only mode: service not available");
                }
            } else {
                if (ui && ui->pushButtonConnect) {
                    ui->pushButtonConnect->setEnabled(true);
                    ui->pushButtonConnect->setToolTip("");
                }
            }
        });
    }

    log("Ready. Enter username and connect.");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::log(const QString &msg)
{
    ui->textEditLog->append(msg);
}

