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
#include "actions/actiondialog.h"
#ifdef Q_OS_WIN
#include <QSettings>
#include <windows.h>
#include <wincrypt.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <objbase.h>
#endif
namespace {
#ifdef Q_OS_WIN
QByteArray encryptWithDpapi(const QByteArray &plain)
{
    if (plain.isEmpty()) return QByteArray();
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"MPM", nullptr, nullptr, nullptr, 0, &out)) {
        return QByteArray();
    }
    QByteArray encrypted(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    if (out.pbData) LocalFree(out.pbData);
    return encrypted.toBase64();
}

QByteArray decryptWithDpapi(const QByteArray &cipherBase64)
{
    if (cipherBase64.isEmpty()) return QByteArray();
    QByteArray cipher = QByteArray::fromBase64(cipherBase64);
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(cipher.data());
    in.cbData = static_cast<DWORD>(cipher.size());
    DATA_BLOB out{};
    LPWSTR description = nullptr;
    if (!CryptUnprotectData(&in, &description, nullptr, nullptr, nullptr, 0, &out)) {
        return QByteArray();
    }
    if (description) LocalFree(description);
    QByteArray plain(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    if (out.pbData) LocalFree(out.pbData);
    return plain;
}
#endif
}


void MainWindow::onAddAction()
{
    ActionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto res = dlg.getResult();
    UserActionCfg a{ res.customName, res.type, res.expectedMessage, res.exePath };
    m_actions.push_back(a);
    saveActions();
    refreshActionsList();
}

void MainWindow::onEditSelectedAction()
{
    const int row = ui->listWidgetActions->currentRow();
    if (row < 0 || row >= m_actions.size()) return;
    ActionDialog dlg(this);
    ActionDialog::Result init{ m_actions[row].customName, m_actions[row].type, m_actions[row].expectedMessage, m_actions[row].exePath };
    dlg.setInitial(init);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto res = dlg.getResult();
    m_actions[row] = UserActionCfg{ res.customName, res.type, res.expectedMessage, res.exePath };
    saveActions();
    refreshActionsList();
}

void MainWindow::onDeleteSelectedAction()
{
    const int row = ui->listWidgetActions->currentRow();
    if (row < 0 || row >= m_actions.size()) return;
    if (QMessageBox::question(this, "Delete Action", "Are you sure?") != QMessageBox::Yes) return;
    m_actions.removeAt(row);
    saveActions();
    refreshActionsList();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, "MPM", "MqttPowerManager")
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

    loadSettings();

    connect(ui->checkBoxStartWithWindows, &QCheckBox::toggled, this, &MainWindow::onStartWithWindowsToggled);
    connect(ui->checkBoxAutoConnect, &QCheckBox::toggled, this, [this](bool){ saveSettingsIfNeeded(); });
    connect(ui->checkBoxStartMinimized, &QCheckBox::toggled, this, [this](bool){ saveSettingsIfNeeded(); });
    // Startup path lock UI
    connect(ui->checkBoxLockStartupPath, &QCheckBox::toggled, this, &MainWindow::onStartupPathLockToggled);
    connect(ui->pushButtonBrowseStartupPath, &QPushButton::clicked, this, &MainWindow::onBrowseStartupExe);
    connect(ui->lineEditStartupPath, &QLineEdit::editingFinished, this, [this]() {
        saveSettingsIfNeeded();
        if (ui->checkBoxStartWithWindows->isChecked()) updateWindowsStartup(true);
    });
    connect(ui->pushButtonCreateStartMenuShortcut, &QPushButton::clicked, this, &MainWindow::onCreateStartMenuShortcut);
    loadActions();
    applyUiToClient();
    updateConnectButton();
    updateStatusLabel(m_client->state());
    updateTrayIconByState();

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
        QTimer::singleShot(0, this, [this]() { onConnectClicked(); });
    }

    // Ensure we publish offline on app exit (graceful)
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        publishAvailabilityOffline();
    });

    // Wire save settings button
    connect(ui->pushButtonSaveSettings, &QPushButton::clicked, this, &MainWindow::onSaveSettingsClicked);

    log("Ready. Enter username and connect.");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onConnectClicked()
{
    saveSettingsIfNeeded();
    applyUiToClient();

    if (m_client->state() == QMqttClient::Disconnected) {
        log("Connecting...");
        const int timeoutSec = ui->spinBoxTimeoutSec->value();
        if (timeoutSec > 0) {
            m_connectTimeoutTimer.start(timeoutSec * 1000);
        }
        // Ensure LWT is configured just before connecting
        updateAvailabilityWill();
        m_client->connectToHost();
    } else {
        log("Disconnecting...");
        m_connectTimeoutTimer.stop();
        // Gracefully publish offline retained before disconnect
        publishAvailabilityOffline();
        m_client->disconnectFromHost();
    }
}

void MainWindow::onConnected()
{
    m_connectTimeoutTimer.stop();
    log("Connected to MQTT broker");

    // Subscribe to topic
    QString topic = getSubscribeTopic();
    if (!topic.isEmpty()) {
        auto subscription = m_client->subscribe(topic, 0);
        if (!subscription) {
            log("Failed to subscribe to " + topic);
        } else {
            log("Subscribed to topic: " + topic);
        }
    } else {
        log("Username/custom ID is empty!");
    }

    // Publish availability online retained so HA sees us immediately
    publishAvailabilityOnline();
}

void MainWindow::onStateChanged(QMqttClient::ClientState state)
{
    updateConnectButton();
    updateTrayIconByState();
    updateStatusLabel(state);
    switch (state) {
    case QMqttClient::Disconnected:
        log("MQTT state: Disconnected");
        break;
    case QMqttClient::Connecting:
        log("MQTT state: Connecting");
        break;
    case QMqttClient::Connected:
        log("MQTT state: Connected");
        break;
    }
}

void MainWindow::onErrorChanged(QMqttClient::ClientError error)
{
    if (error == QMqttClient::NoError) return;
    log(QString("MQTT error: %1").arg(static_cast<int>(error)));
    updateStatusLabel(m_client->state(), error);
}

void MainWindow::onConnectTimeout()
{
    if (m_client->state() == QMqttClient::Connecting) {
        log("Connection timed out. Aborting.");
        m_client->disconnectFromHost();
    }
}

void MainWindow::onSaveSettingsClicked()
{
    saveSettingsIfNeeded();
    // Display the current settings to the log/output as a simple confirmation
    QStringList lines;
    lines << "Settings saved:";
    lines << QString("- Username/Custom ID: %1").arg(ui->lineEditUsername->text());
    lines << QString("- Host: %1").arg(ui->lineEditHost->text());
    lines << QString("- Port: %1").arg(ui->spinBoxPort->value());
    lines << QString("- MQTT Username: %1").arg(ui->lineEditMqttUsername->text());
    lines << QString("- Remember: %1").arg(ui->checkBoxRemember->isChecked() ? "true" : "false");
    lines << QString("- Print only: %1").arg(ui->checkBoxPrintOnly->isChecked() ? "true" : "false");
    lines << QString("- Timeout (s): %1").arg(ui->spinBoxTimeoutSec->value());
    lines << QString("- Start with Windows: %1").arg(ui->checkBoxStartWithWindows->isChecked() ? "true" : "false");
    lines << QString("- Auto-connect: %1").arg(ui->checkBoxAutoConnect->isChecked() ? "true" : "false");
    lines << QString("- Start minimized: %1").arg(ui->checkBoxStartMinimized->isChecked() ? "true" : "false");
    log(lines.join('\n'));
}

QString MainWindow::getSubscribeTopic() const
{
    QString username = ui->lineEditUsername->text().trimmed();
    if (username.isEmpty()) return QString();
    // Subscribe to all actions for this user
    return QString("mqttpowermanager/%1/+" ).arg(username);
}

void MainWindow::onMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString msg = QString::fromUtf8(message);
    log("Received message: " + msg + " on topic: " + topic.name());

    if (ui->checkBoxPrintOnly->isChecked()) {
        log("Print only mode enabled — ignoring commands.");
        return;
    }

    // Topic format: mqttpowermanager/%1/<action_name>
    const QStringList parts = topic.name().split('/');
    QString actionName = parts.size() >= 3 ? parts.last() : QString();

    if (ui->checkBoxPrintOnly->isChecked()) {
        return;
    }

    // Find configured action matching actionName and expected message
    auto it = std::find_if(m_actions.begin(), m_actions.end(), [&](const UserActionCfg &a){
        return a.customName.compare(actionName, Qt::CaseInsensitive) == 0
               && msg.compare(a.expectedMessage, Qt::CaseInsensitive) == 0;
    });
    if (it == m_actions.end()) {
        log("Message ignored (no matching configured action).");
        return;
    }

    if (!ActionsRegistry::execute(it->type, it->exePath)) {
        log("Action executed as no-op or not supported on this OS.");
    }
}

void MainWindow::log(const QString &msg)
{
    ui->textEditLog->append(msg);
}

void MainWindow::updateStatusLabel(QMqttClient::ClientState state, QMqttClient::ClientError error)
{
    if (!ui->labelStatus) return;
    QString text;
    QString colorCss;
    if (error != QMqttClient::NoError) {
        text = "Status: Error";
        colorCss = "color: red;";
    } else {
        switch (state) {
        case QMqttClient::Connected:
            text = "Status: Connected \xF0\x9F\x9F\xA2"; // green circle
            colorCss = "color: green;"; // green text
            break;
        case QMqttClient::Connecting:
            text = "Status: Connecting";
            colorCss = "color: white;";
            break;
        case QMqttClient::Disconnected:
        default:
            text = "Status: Disconnected";
            colorCss = "color: white;";
            break;
        }
    }
    ui->labelStatus->setText(text);
    ui->labelStatus->setStyleSheet(colorCss);
}

void MainWindow::loadSettings()
{
    // General
    ui->lineEditUsername->setText(m_settings.value("user/customId").toString());

    // MQTT
    ui->lineEditHost->setText(m_settings.value("mqtt/host", "127.0.0.1").toString());
    ui->spinBoxPort->setValue(m_settings.value("mqtt/port", 1883).toInt());
    ui->lineEditMqttUsername->setText(m_settings.value("mqtt/username").toString());
#ifdef Q_OS_WIN
    {
        QByteArray enc = m_settings.value("mqtt/passwordEnc").toByteArray();
        if (enc.isEmpty()) {
            // Migrate legacy plaintext if present
            const QString legacy = m_settings.value("mqtt/password").toString();
            if (!legacy.isEmpty()) {
                QByteArray cipher = encryptWithDpapi(legacy.toUtf8());
                if (!cipher.isEmpty()) {
                    m_settings.setValue("mqtt/passwordEnc", cipher);
                    m_settings.remove("mqtt/password");
                    ui->lineEditMqttPassword->setText(legacy);
                } else {
                    ui->lineEditMqttPassword->setText(legacy);
                }
            } else {
                ui->lineEditMqttPassword->clear();
            }
        } else {
            QByteArray plain = decryptWithDpapi(enc);
            ui->lineEditMqttPassword->setText(QString::fromUtf8(plain));
        }
    }
#else
    // Non-Windows fallback: keep legacy behavior
    ui->lineEditMqttPassword->setText(m_settings.value("mqtt/password").toString());
#endif

    // Options
    ui->checkBoxRemember->setChecked(m_settings.value("options/remember", true).toBool());
    ui->checkBoxPrintOnly->setChecked(m_settings.value("options/printOnly", false).toBool());
    ui->spinBoxTimeoutSec->setValue(m_settings.value("options/timeoutSec", 5).toInt());
    ui->checkBoxStartWithWindows->setChecked(m_settings.value("options/startWithWindows", false).toBool());
    ui->checkBoxAutoConnect->setChecked(m_settings.value("options/autoConnect", false).toBool());
    ui->checkBoxStartMinimized->setChecked(m_settings.value("options/startMinimized", false).toBool());
    // Startup path lock
    const bool lockPath = m_settings.value("options/startupPathLocked", false).toBool();
    ui->checkBoxLockStartupPath->setChecked(lockPath);
    const QString defaultExe = QCoreApplication::applicationFilePath();
    ui->lineEditStartupPath->setText(m_settings.value("options/startupPath", defaultExe).toString());
    ui->lineEditStartupPath->setEnabled(lockPath);
    ui->pushButtonBrowseStartupPath->setEnabled(lockPath);
}

void MainWindow::saveSettingsIfNeeded()
{
    const bool remember = ui->checkBoxRemember->isChecked();
    m_settings.setValue("options/remember", remember);

    // Always persist non-sensitive options
    m_settings.setValue("options/printOnly", ui->checkBoxPrintOnly->isChecked());
    m_settings.setValue("options/timeoutSec", ui->spinBoxTimeoutSec->value());
    m_settings.setValue("options/autoConnect", ui->checkBoxAutoConnect->isChecked());
    m_settings.setValue("options/startWithWindows", ui->checkBoxStartWithWindows->isChecked());
    m_settings.setValue("options/startMinimized", ui->checkBoxStartMinimized->isChecked());
    m_settings.setValue("options/startupPathLocked", ui->checkBoxLockStartupPath->isChecked());
    m_settings.setValue("options/startupPath", ui->lineEditStartupPath->text());

    if (!remember) return;

    // General
    m_settings.setValue("user/customId", ui->lineEditUsername->text());

    // MQTT
    m_settings.setValue("mqtt/host", ui->lineEditHost->text());
    m_settings.setValue("mqtt/port", ui->spinBoxPort->value());
    m_settings.setValue("mqtt/username", ui->lineEditMqttUsername->text());
#ifdef Q_OS_WIN
    {
        const QString pw = ui->lineEditMqttPassword->text();
        if (pw.isEmpty()) {
            m_settings.remove("mqtt/passwordEnc");
        } else {
            QByteArray enc = encryptWithDpapi(pw.toUtf8());
            if (!enc.isEmpty()) {
                m_settings.setValue("mqtt/passwordEnc", enc);
            }
        }
        // Ensure legacy key is removed when remember is on
        m_settings.remove("mqtt/password");
    }
#else
    // Non-Windows fallback
    m_settings.setValue("mqtt/password", ui->lineEditMqttPassword->text());
#endif
}

void MainWindow::applyUiToClient()
{
    m_client->setHostname(ui->lineEditHost->text().trimmed());
    m_client->setPort(static_cast<quint16>(ui->spinBoxPort->value()));

    // MQTT auth
    m_client->setUsername(ui->lineEditMqttUsername->text());
    m_client->setPassword(ui->lineEditMqttPassword->text());
}

QString MainWindow::getAvailabilityTopic() const
{
    const QString username = ui->lineEditUsername->text().trimmed();
    if (username.isEmpty()) return QString();
    return QString("mqttpowermanager/%1/health").arg(username);
}

void MainWindow::updateAvailabilityWill()
{
    const QString topic = getAvailabilityTopic();
    if (topic.isEmpty()) return;

    m_client->setWillTopic(topic);
    m_client->setWillMessage(QByteArrayLiteral("offline"));
    m_client->setWillQoS(0);
    m_client->setWillRetain(true);
}

void MainWindow::publishAvailabilityOnline()
{
    const QString topic = getAvailabilityTopic();
    if (topic.isEmpty()) return;
    m_client->publish(topic, QByteArrayLiteral("online"), 0, true);
}

void MainWindow::publishAvailabilityOffline()
{
    const QString topic = getAvailabilityTopic();
    if (topic.isEmpty()) return;
    // Try to publish retained offline if connected
    if (m_client->state() == QMqttClient::Connected) {
        m_client->publish(topic, QByteArrayLiteral("offline"), 0, true);
    }
}

// Actions persistence and UI
void MainWindow::loadActions()
{
    m_actions.clear();
    int size = m_settings.beginReadArray("actions");
    for (int i = 0; i < size; ++i) {
        m_settings.setArrayIndex(i);
        UserActionCfg a;
        a.customName = m_settings.value("name").toString();
        a.expectedMessage = m_settings.value("message", "PRESS").toString();
        a.exePath = m_settings.value("exePath").toString();
        QString typeStr = m_settings.value("type", "Shutdown").toString();
        ActionType t;
        if (!ActionsRegistry::fromString(typeStr, t)) t = ActionType::Shutdown;
        a.type = t;
        if (!a.customName.isEmpty()) m_actions.push_back(a);
    }
    m_settings.endArray();
    refreshActionsList();
}

void MainWindow::saveActions()
{
    m_settings.beginWriteArray("actions");
    for (int i = 0; i < m_actions.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("name", m_actions[i].customName);
        m_settings.setValue("message", m_actions[i].expectedMessage);
        m_settings.setValue("exePath", m_actions[i].exePath);
        m_settings.setValue("type", ActionsRegistry::toString(m_actions[i].type));
    }
    m_settings.endArray();
}

void MainWindow::refreshActionsList()
{
    if (!ui->listWidgetActions) return;
    const QSignalBlocker blocker(ui->listWidgetActions);
    const int previousRow = ui->listWidgetActions->currentRow();
    ui->listWidgetActions->clear();
    for (const auto &a : m_actions) {
        ui->listWidgetActions->addItem(a.customName);
    }
    int row = previousRow >= 0 ? previousRow : 0;
    if (row >= ui->listWidgetActions->count()) row = ui->listWidgetActions->count() - 1;
    if (row < 0) row = 0;
    if (ui->listWidgetActions->count() > 0) {
        ui->listWidgetActions->setCurrentRow(row);
    }
    updateActionsFooter();
}

void MainWindow::updateActionsFooter()
{
    if (!ui->listWidgetActions) return;
    const int row = ui->listWidgetActions->currentRow();
    if (row >= 0 && row < m_actions.size()) {
        const auto &a = m_actions[row];
        const QString username = ui->lineEditUsername->text().trimmed();
        const QString topic = username.isEmpty()
            ? QString("mqttpowermanager/%1/").arg("<username>") + a.customName
            : QString("mqttpowermanager/%1/").arg(username) + a.customName;
        ui->labelActionsInfo->setText("Expected MQTT message: " + a.expectedMessage +
            " | Topic: " + topic);
    } else {
        ui->labelActionsInfo->setText("Expected MQTT message: ");
    }
}

void MainWindow::onActionsContextMenu(const QPoint &pos)
{
    if (!ui->listWidgetActions) return;
    const int row = ui->listWidgetActions->currentRow();
    if (row < 0 || row >= m_actions.size()) return;
    const auto &a = m_actions[row];

    QMenu menu(this);
    QAction *copyTopic = menu.addAction("Copy MQTT topic");
    QAction *copyPublish = menu.addAction("Copy publish command");

    QAction *chosen = menu.exec(ui->listWidgetActions->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    const QString username = ui->lineEditUsername->text().trimmed();
    const QString topic = username.isEmpty()
        ? QString("mqttpowermanager/%1/").arg("<username>") + a.customName
        : QString("mqttpowermanager/%1/").arg(username) + a.customName;

    QString textToCopy;
    if (chosen == copyTopic) {
        textToCopy = topic;
    } else if (chosen == copyPublish) {
        // Generic mosquitto_pub command; user can fill broker/creds if needed
        textToCopy = QString("mosquitto_pub -h %1 -p %2 -t \"%3\" -m \"%4\"")
                         .arg(ui->lineEditHost->text().trimmed())
                         .arg(ui->spinBoxPort->value())
                         .arg(topic)
                         .arg(a.expectedMessage);
    }

    if (!textToCopy.isEmpty()) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(textToCopy);
    }
}

 

// Tray
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
        qApp->quit();
    });
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("MPM");
    m_trayIcon->setVisible(true);
    updateTrayShowHideText();
    updateTrayIconByState();
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
    switch (m_client->state()) {
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

// Windows startup
void MainWindow::onStartWithWindowsToggled(bool enabled)
{
    updateWindowsStartup(enabled);
    if (ui->checkBoxRemember->isChecked()) {
        m_settings.setValue("options/startWithWindows", enabled);
    }
}

void MainWindow::updateWindowsStartup(bool enabled)
{
#ifdef Q_OS_WIN
    // Use registry Run key
    QString appName = "MPM";
    QString appPath = QDir::toNativeSeparators(
        (ui->checkBoxLockStartupPath->isChecked() && !ui->lineEditStartupPath->text().trimmed().isEmpty())
            ? ui->lineEditStartupPath->text().trimmed()
            : QCoreApplication::applicationFilePath());
    QSettings runKey("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        runKey.setValue(appName, '"' + appPath + '"');
    } else {
        runKey.remove(appName);
    }
#else
    Q_UNUSED(enabled);
#endif
}

void MainWindow::onStartupPathLockToggled(bool locked)
{
    ui->lineEditStartupPath->setEnabled(locked);
    ui->pushButtonBrowseStartupPath->setEnabled(locked);
    saveSettingsIfNeeded();
    if (ui->checkBoxStartWithWindows->isChecked()) updateWindowsStartup(true);
}

void MainWindow::onBrowseStartupExe()
{
    QString initial = ui->lineEditStartupPath->text().trimmed();
    if (initial.isEmpty()) initial = QCoreApplication::applicationFilePath();
    const QString path = QFileDialog::getOpenFileName(this, "Select executable for startup", QFileInfo(initial).absolutePath(),
                                                      "Executables (*.exe);;All files (*.*)");
    if (!path.isEmpty()) {
        ui->lineEditStartupPath->setText(path);
        saveSettingsIfNeeded();
        if (ui->checkBoxStartWithWindows->isChecked()) updateWindowsStartup(true);
    }
}

void MainWindow::onCreateStartMenuShortcut()
{
#ifdef Q_OS_WIN
    // Resolve target path based on lock setting
    const QString targetPath = (ui->checkBoxLockStartupPath->isChecked() && !ui->lineEditStartupPath->text().trimmed().isEmpty())
                                   ? ui->lineEditStartupPath->text().trimmed()
                                   : QCoreApplication::applicationFilePath();
    const QString nativeTarget = QDir::toNativeSeparators(targetPath);

    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (appsDir.isEmpty()) {
        QMessageBox::warning(this, "Start Menu", "Cannot resolve Start Menu folder.");
        return;
    }
    QDir().mkpath(appsDir);
    const QString linkPath = QDir(appsDir).filePath("MPM.lnk");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(hr);
    IShellLinkW *psl = nullptr;
    HRESULT hrc = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&psl));
    if (SUCCEEDED(hrc) && psl) {
        psl->SetPath((LPCWSTR)nativeTarget.utf16());
        const QString workDir = QFileInfo(nativeTarget).absolutePath();
        psl->SetWorkingDirectory((LPCWSTR)workDir.utf16());
        psl->SetDescription(L"MPM");
        IPersistFile *ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&ppf))) && ppf) {
            const std::wstring wlink = linkPath.toStdWString();
            if (SUCCEEDED(ppf->Save(wlink.c_str(), TRUE))) {
                QMessageBox::information(this, "Start Menu", "Shortcut created in Start Menu.");
            } else {
                QMessageBox::warning(this, "Start Menu", "Failed to save shortcut.");
            }
            ppf->Release();
        } else {
            QMessageBox::warning(this, "Start Menu", "Failed to access IPersistFile.");
        }
        psl->Release();
    } else {
        QMessageBox::warning(this, "Start Menu", "Failed to create IShellLink.");
    }
    if (shouldUninit) CoUninitialize();
#else
    QMessageBox::information(this, "Start Menu", "This feature is only available on Windows.");
#endif
}

// Optional: handle close to tray
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        updateTrayShowHideText();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::updateConnectButton()
{
    switch (m_client->state()) {
    case QMqttClient::Disconnected:
        ui->pushButtonConnect->setText("Connect");
        break;
    case QMqttClient::Connecting:
        ui->pushButtonConnect->setText("Connecting...");
        break;
    case QMqttClient::Connected:
        ui->pushButtonConnect->setText("Disconnect");
        break;
    }
}
