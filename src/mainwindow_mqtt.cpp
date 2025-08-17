#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTimer>
#include <QClipboard>
#include <QApplication>
#include "common/service_ipc_client.h"
#include "common/crypto_win.h"
#include <QSettings>

void MainWindow::onConnectClicked()
{
    saveAllSettingsForce();
    applyUiToClient();

    if (m_isControllingService || ServiceIpcClient::isAvailable()) {
        m_isControllingService = true;
        ServiceIpcClient::sendPreferred(m_preferredIpc, QByteArrayLiteral("reload-settings"), QStringLiteral("MPMServiceIpc"), 200);
        if (m_serviceState == QMqttClient::Disconnected) {
            log("Service: connect requested");
            ServiceIpcClient::sendPreferred(m_preferredIpc, QByteArrayLiteral("connect"), QStringLiteral("MPMServiceIpc"), 200);
        } else if (m_serviceState == QMqttClient::Connected) {
            log("Service: disconnect requested");
            ServiceIpcClient::sendPreferred(m_preferredIpc, QByteArrayLiteral("disconnect"), QStringLiteral("MPMServiceIpc"), 200);
        } else {
            log("Service: busy");
        }
        return;
    }

    if (m_client->state() == QMqttClient::Disconnected) {
        if (m_reconnectTimer.isActive()) {
            m_reconnectTimer.stop();
            log("Auto-reconnect canceled.");
            updateConnectButton();
            return;
        }
        log("Connecting...");
        m_userInitiatedDisconnect = false;
        const int timeoutSec = ui->spinBoxTimeoutSec->value();
        if (timeoutSec > 0) {
            m_connectTimeoutTimer.start(timeoutSec * 1000);
        }
        updateAvailabilityWill();
        m_client->connectToHost();
        m_reconnectTimer.stop();
    } else {
        log("Disconnecting...");
        m_connectTimeoutTimer.stop();
        publishAvailabilityOffline();
        m_userInitiatedDisconnect = true;
        m_client->disconnectFromHost();
        m_reconnectTimer.stop();
    }
}

void MainWindow::onConnected()
{
    m_connectTimeoutTimer.stop();
    log("Connected to MQTT broker");

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
        if (!m_userInitiatedDisconnect) {
            scheduleReconnectIfNeeded();
        }
        break;
    case QMqttClient::Connecting:
        log("MQTT state: Connecting");
        break;
    case QMqttClient::Connected:
        log("MQTT state: Connected");
        m_reconnectTimer.stop();
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
    saveAllSettingsForce();
    QStringList lines;
    log("Settings saved");

    if (m_isControllingService || ServiceIpcClient::isAvailable()) {
        // Push settings and let service hot-apply without disconnecting
        const QByteArray r = ServiceIpcClient::send("reload-settings");
        if (r == "ok") log("Service: settings reloaded"); else log("Service: failed to reload settings");
    }
}

void MainWindow::scheduleReconnectIfNeeded()
{
    if (!ui->checkBoxAutoReconnect->isChecked()) return;
    if (m_client->state() != QMqttClient::Disconnected) return;
    if (m_userInitiatedDisconnect) return;
    if (m_reconnectTimer.isActive()) return;
    const int delaySec = std::max(1, ui->spinBoxReconnectSec->value());
    log(QString("Scheduling auto-reconnect in %1 s").arg(delaySec));
    m_reconnectTimer.start(delaySec * 1000);
    updateConnectButton();
}

QString MainWindow::getSubscribeTopic() const
{
    QString username = ui->lineEditUsername->text().trimmed();
    if (username.isEmpty()) return QString();
    return QString("mqttpowermanager/%1/+" ).arg(username);
}

void MainWindow::onMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString msg = QString::fromUtf8(message);
    if (topic.name().endsWith("/health")) {
        return;
    }
    log("Received message: " + msg + " on topic: " + topic.name());
    if (ui->checkBoxPrintOnly->isChecked()) {
        log("Print only mode enabled — ignoring commands.");
        return;
    }
    const QStringList parts = topic.name().split('/');
    QString actionName = parts.size() >= 3 ? parts.last() : QString();
    if (ui->checkBoxPrintOnly->isChecked()) {
        return;
    }
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
            text = "Status: Connected \xF0\x9F\x9F\xA2";
            colorCss = "color: green;";
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
    ui->lineEditUsername->setText(m_settings.value("user/customId").toString());
    ui->lineEditHost->setText(m_settings.value("mqtt/host", "127.0.0.1").toString());
    ui->spinBoxPort->setValue(m_settings.value("mqtt/port", 1883).toInt());
    ui->lineEditMqttUsername->setText(m_settings.value("mqtt/username").toString());
    {
        QByteArray enc = m_settings.value("mqtt/passwordEnc").toByteArray();
        if (enc.isEmpty()) {
            const QString legacy = m_settings.value("mqtt/password").toString();
            if (!legacy.isEmpty()) {
                QByteArray cipher = dpapiEncryptMachineScope(legacy.toUtf8());
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
            QByteArray plain = dpapiDecryptBase64(enc);
            ui->lineEditMqttPassword->setText(QString::fromUtf8(plain));
            if (!plain.isEmpty()) {
                QByteArray machineEnc = dpapiEncryptMachineScope(plain);
                if (!machineEnc.isEmpty()) {
                    m_settings.setValue("mqtt/passwordEnc", machineEnc);
                }
            }
        }
    }
    ui->checkBoxPrintOnly->setChecked(m_settings.value("options/printOnly", false).toBool());
    ui->spinBoxTimeoutSec->setValue(m_settings.value("options/timeoutSec", 5).toInt());
    ui->checkBoxStartWithWindows->setChecked(m_settings.value("options/startWithWindows", false).toBool());
    ui->checkBoxAutoConnect->setChecked(m_settings.value("options/autoConnect", false).toBool());
    ui->checkBoxAutoReconnect->setChecked(m_settings.value("options/autoReconnect", false).toBool());
    ui->spinBoxReconnectSec->setValue(m_settings.value("options/reconnectSec", 5).toInt());
    ui->checkBoxStartMinimized->setChecked(m_settings.value("options/startMinimized", false).toBool());
    const bool lockPath = m_settings.value("options/startupPathLocked", false).toBool();
    ui->checkBoxLockStartupPath->setChecked(lockPath);
    const QString defaultExe = QCoreApplication::applicationFilePath();
    ui->lineEditStartupPath->setText(m_settings.value("options/startupPath", defaultExe).toString());
    ui->lineEditStartupPath->setEnabled(lockPath);
    ui->pushButtonBrowseStartupPath->setEnabled(lockPath);
}

void MainWindow::applyUiToClient()
{
    m_client->setHostname(ui->lineEditHost->text().trimmed());
    m_client->setPort(static_cast<quint16>(ui->spinBoxPort->value()));
    m_client->setUsername(ui->lineEditMqttUsername->text());
    m_client->setPassword(ui->lineEditMqttPassword->text());
}

void MainWindow::saveAllSettingsForce()
{
    // Persist all UI values regardless of Remember and current flags
    m_settings.setValue("user/customId", ui->lineEditUsername->text());
    m_settings.setValue("mqtt/host", ui->lineEditHost->text());
    m_settings.setValue("mqtt/port", ui->spinBoxPort->value());
    m_settings.setValue("mqtt/username", ui->lineEditMqttUsername->text());
    // Password saved encrypted for service-side decryption
    {
        const QString pw = ui->lineEditMqttPassword->text();
        if (pw.isEmpty()) {
            m_settings.remove("mqtt/passwordEnc");
        } else {
            QByteArray enc = dpapiEncryptMachineScope(pw.toUtf8());
            if (!enc.isEmpty()) m_settings.setValue("mqtt/passwordEnc", enc);
        }
        m_settings.remove("mqtt/password");
    }
    // Options
    m_settings.setValue("options/printOnly", ui->checkBoxPrintOnly->isChecked());
    m_settings.setValue("options/timeoutSec", ui->spinBoxTimeoutSec->value());
    m_settings.setValue("options/autoConnect", ui->checkBoxAutoConnect->isChecked());
    m_settings.setValue("options/autoReconnect", ui->checkBoxAutoReconnect->isChecked());
    m_settings.setValue("options/reconnectSec", ui->spinBoxReconnectSec->value());
    m_settings.setValue("options/startWithWindows", ui->checkBoxStartWithWindows->isChecked());
    m_settings.setValue("options/startMinimized", ui->checkBoxStartMinimized->isChecked());
    m_settings.setValue("options/startupPathLocked", ui->checkBoxLockStartupPath->isChecked());
    m_settings.setValue("options/startupPath", ui->lineEditStartupPath->text());
    // Actions array
    m_settings.beginWriteArray("actions");
    for (int i = 0; i < m_actions.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("name", m_actions[i].customName);
        m_settings.setValue("message", m_actions[i].expectedMessage);
        m_settings.setValue("exePath", m_actions[i].exePath);
        m_settings.setValue("type", ActionsRegistry::toString(m_actions[i].type));
    }
    m_settings.endArray();
    m_settings.sync();
}

void MainWindow::saveSettingsIfNeeded()
{
    // Simplified: always save everything
    saveAllSettingsForce();
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
    if (m_client->state() == QMqttClient::Connected) {
        m_client->publish(topic, QByteArrayLiteral("offline"), 0, true);
    }
}

void MainWindow::updateConnectButton()
{
    switch (m_client->state()) {
    case QMqttClient::Disconnected:
        if (m_reconnectTimer.isActive()) {
            ui->pushButtonConnect->setText("Connecting...");
            ui->pushButtonConnect->setToolTip("Click to cancel auto-reconnect");
        } else {
            ui->pushButtonConnect->setText("Connect");
            ui->pushButtonConnect->setToolTip("");
        }
        break;
    case QMqttClient::Connecting:
        ui->pushButtonConnect->setText("Connecting...");
        ui->pushButtonConnect->setToolTip("");
        break;
    case QMqttClient::Connected:
        ui->pushButtonConnect->setText("Disconnect");
        ui->pushButtonConnect->setToolTip("");
        break;
    }
}


