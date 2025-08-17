#include "mqtt_daemon.h"
#include "../common/settings.h"
#include "../common/crypto_win.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

MqttDaemon::MqttDaemon(QObject *parent)
	: QObject(parent)
	, m_settings(mpmCreateSharedSettings())
{
	m_client = new QMqttClient(this);
	connect(m_client, &QMqttClient::connected, this, &MqttDaemon::onConnected);
	connect(m_client, &QMqttClient::messageReceived, this, &MqttDaemon::onMessageReceived);
	connect(m_client, &QMqttClient::stateChanged, this, &MqttDaemon::onStateChanged);
	connect(m_client, &QMqttClient::errorChanged, this, &MqttDaemon::onErrorChanged);
	m_reconnectTimer = new QTimer(this);
	m_reconnectTimer->setSingleShot(false);
}

void MqttDaemon::start()
{
	loadSettings();
	applyToClient();
	if (m_autoConnect) {
		qInfo() << "Auto-connect enabled";
		m_userInitiatedDisconnect = false;
		m_client->connectToHost();
	}
	// Auto-reconnect loop
	m_reconnectTimer->setInterval(qMax(1000, m_reconnectSec * 1000));
	connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
		if (!m_autoReconnect) return;
		if (m_userInitiatedDisconnect) return;
		if (m_client->state() == QMqttClient::Disconnected) {
			qInfo() << "MQTT reconnecting...";
			m_client->connectToHost();
		}
	});
}

void MqttDaemon::reloadSettings()
{
	qInfo() << "Reloading settings";
	m_settings.sync();
	const QString oldHost = m_host;
	const quint16 oldPort = m_port;
	const QString oldUser = m_mqttUser;
	const QString oldPass = m_mqttPassword;
	const bool oldAutoReconnect = m_autoReconnect;
	// Fresh QSettings to force reread
	QSettings fresh(mpmSharedSettingsFilePath(), QSettings::IniFormat);
	loadSettings(&fresh);
	applyToClient();
	m_reconnectTimer->setInterval(qMax(1000, m_reconnectSec * 1000));
	// If connection parameters changed and we're connected, reconnect to apply
	if (m_client->state() == QMqttClient::Connected && (m_host != oldHost || m_port != oldPort || m_mqttUser != oldUser || m_mqttPassword != oldPass)) {
		qInfo() << "Connection params changed; reconnecting";
		m_userInitiatedDisconnect = false;
		m_client->disconnectFromHost();
		// Let onStateChanged schedule reconnect
	}
	// If autoConnect is enabled and we're disconnected, connect now
	if (m_autoConnect && m_client->state() == QMqttClient::Disconnected) {
		m_userInitiatedDisconnect = false;
		m_client->connectToHost();
	}
	// If autoReconnect toggled on while disconnected, start timer
	if (!oldAutoReconnect && m_autoReconnect && m_client->state() == QMqttClient::Disconnected && !m_userInitiatedDisconnect) {
		m_reconnectTimer->start();
	}
}

void MqttDaemon::loadSettings(QSettings *source)
{
	QSettings &S = source ? *source : m_settings;
	m_username = S.value("user/customId").toString().trimmed();
	m_host = S.value("mqtt/host", "127.0.0.1").toString();
	m_port = static_cast<quint16>(S.value("mqtt/port", 1883).toInt());
	m_mqttUser = S.value("mqtt/username").toString();
	{
		const QByteArray enc = S.value("mqtt/passwordEnc").toByteArray();
		QByteArray plain = dpapiDecryptBase64(enc);
		if (plain.isEmpty()) {
			// Fallback to legacy plaintext if present
			m_mqttPassword = S.value("mqtt/password").toString();
		} else {
			m_mqttPassword = QString::fromUtf8(plain);
		}
	}
	m_autoConnect = S.value("options/autoConnect", false).toBool();
	m_autoReconnect = S.value("options/autoReconnect", false).toBool();
	m_reconnectSec = qMax(1, S.value("options/reconnectSec", 5).toInt());
	m_printOnly = S.value("options/printOnly", false).toBool();
	loadActions(&S);
}

void MqttDaemon::applyToClient()
{
	m_client->setHostname(m_host);
	m_client->setPort(m_port);
	m_client->setClientId("MPMService");
	m_client->setUsername(m_mqttUser);
	m_client->setPassword(m_mqttPassword);
	// LWT
	const QString topic = availabilityTopic();
	if (!topic.isEmpty()) {
		m_client->setWillTopic(topic);
		m_client->setWillMessage(QByteArrayLiteral("offline"));
		m_client->setWillQoS(0);
		m_client->setWillRetain(true);
	}
}

QString MqttDaemon::subscribeTopic() const
{
	if (m_username.isEmpty()) return QString();
	return QString("mqttpowermanager/%1/+" ).arg(m_username);
}

QString MqttDaemon::availabilityTopic() const
{
	if (m_username.isEmpty()) return QString();
	return QString("mqttpowermanager/%1/health").arg(m_username);
}

void MqttDaemon::onConnected()
{
	const QString topic = subscribeTopic();
	if (!topic.isEmpty()) {
		qInfo() << "Subscribed to" << topic;
		m_client->subscribe(topic, 0);
	} else {
		qWarning() << "Username/customId is empty; no subscription";
	}
	if (m_client->state() == QMqttClient::Connected) {
		m_client->publish(availabilityTopic(), QByteArrayLiteral("online"), 0, true);
	}
	m_userInitiatedDisconnect = false;
}

void MqttDaemon::onStateChanged(QMqttClient::ClientState state)
{
	switch (state) {
	case QMqttClient::Disconnected: qInfo() << "MQTT state: Disconnected"; break;
	case QMqttClient::Connecting: qInfo() << "MQTT state: Connecting"; break;
	case QMqttClient::Connected: qInfo() << "MQTT state: Connected"; break;
	}
	// Manage reconnect timer based on state and flags
	if (!m_autoReconnect || m_userInitiatedDisconnect) {
		m_reconnectTimer->stop();
	} else {
		if (state == QMqttClient::Disconnected) m_reconnectTimer->start();
		else m_reconnectTimer->stop();
	}
}

void MqttDaemon::onErrorChanged(QMqttClient::ClientError error)
{
	if (error != QMqttClient::NoError) qWarning() << "MQTT error:" << static_cast<int>(error);
}

void MqttDaemon::publishAvailabilityOnline()
{
	const QString topic = availabilityTopic();
	if (topic.isEmpty()) return;
	if (m_client->state() == QMqttClient::Connected) {
		m_client->publish(topic, QByteArrayLiteral("online"), 0, true);
	}
}

void MqttDaemon::publishAvailabilityOffline()
{
	const QString topic = availabilityTopic();
	if (topic.isEmpty()) return;
	if (m_client->state() == QMqttClient::Connected) {
		m_client->publish(topic, QByteArrayLiteral("offline"), 0, true);
	}
}

void MqttDaemon::notifyGoingOffline()
{
	qInfo() << "Service shutting down: publishing offline";
	publishAvailabilityOffline();
}

void MqttDaemon::loadActions(QSettings *source)
{
	m_actions.clear();
	QSettings &S = source ? *source : m_settings;
	int size = S.beginReadArray("actions");
	for (int i = 0; i < size; ++i) {
		S.setArrayIndex(i);
		UserActionCfg a;
		a.customName = S.value("name").toString();
		a.expectedMessage = S.value("message", "PRESS").toString();
		a.exePath = S.value("exePath").toString();
		QString typeStr = S.value("type", "Shutdown").toString();
		ActionType t;
		if (!ActionsRegistry::fromString(typeStr, t)) t = ActionType::Shutdown;
		a.type = t;
		if (!a.customName.isEmpty()) m_actions.push_back(a);
	}
	S.endArray();
}

void MqttDaemon::onMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
	const QString msg = QString::fromUtf8(message);
	if (topic.name().endsWith("/health")) return;
	qInfo() << "Received message:" << msg << "on topic:" << topic.name();
	const QStringList parts = topic.name().split('/');
	const QString actionName = parts.size() >= 3 ? parts.last() : QString();
	if (m_printOnly) { qInfo() << "Print only mode enabled — ignoring commands."; return; }
	auto it = std::find_if(m_actions.begin(), m_actions.end(), [&](const UserActionCfg &a){
		return a.customName.compare(actionName, Qt::CaseInsensitive) == 0 &&
		       msg.compare(a.expectedMessage, Qt::CaseInsensitive) == 0;
	});
	if (it == m_actions.end()) {
		qInfo() << "Message ignored" << msg << "topic" << topic.name();
		return;
	}
	const QString typeStr = ActionsRegistry::toString(it->type);
	qInfo() << "Executing action name=" << it->customName
	       << "type=" << typeStr
	       << "expectedMsg=" << it->expectedMessage
	       << "topic=" << topic.name()
	       << "exePath=" << it->exePath;
	const bool ok = ActionsRegistry::execute(it->type, it->exePath);
	if (!ok) {
		qWarning() << "Action execution returned false for" << typeStr << "exePath=" << it->exePath;
	}
}


