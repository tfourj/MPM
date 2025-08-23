#pragma once

#include <QObject>
#include <QMqttClient>
#include <QSettings>
#include <QMqttTopicName>
#include <QVector>
#include <QTimer>
#include "actions/actions.h"

// Headless MQTT daemon used by the Windows Service; reuses settings and actions from shared INI.
class MqttDaemon : public QObject {
	Q_OBJECT
public:
	explicit MqttDaemon(QObject *parent = nullptr);
	void start();

	// Control & status
	QMqttClient::ClientState state() const { return m_client ? m_client->state() : QMqttClient::Disconnected; }
	void forceConnect() { if (m_client && m_client->state() != QMqttClient::Connected) { m_userInitiatedDisconnect = false; m_client->connectToHost(); } }
	void forceDisconnect() {
		if (!m_client) return;
		// Always mark as user-initiated and stop auto-reconnect loop
		m_userInitiatedDisconnect = true;
		if (m_reconnectTimer) m_reconnectTimer->stop();
		publishAvailabilityOffline();
		if (m_client->state() != QMqttClient::Disconnected) {
			m_client->disconnectFromHost();
		}
	}
	void reloadSettings();
	void notifyGoingOffline();

	// Extended status helpers
	bool isReconnectActive() const { return m_reconnectTimer && m_reconnectTimer->isActive(); }
	bool isAutoReconnectEnabled() const { return m_autoReconnect; }
	bool isUserInitiatedDisconnect() const { return m_userInitiatedDisconnect; }

private slots:
	void onConnected();
	void onMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
	void onStateChanged(QMqttClient::ClientState state);
	void onErrorChanged(QMqttClient::ClientError error);

private:
	void loadSettings(QSettings *source = nullptr);
	void applyToClient();
	QString subscribeTopic() const;
	QString availabilityTopic() const;
	void publishAvailabilityOnline();
	void publishAvailabilityOffline();

	struct UserActionCfg {
		QString customName;
		ActionType type;
		QString expectedMessage;
		QString exePath;
	};
	void loadActions(QSettings *source = nullptr);

	QMqttClient *m_client = nullptr;
	QSettings m_settings;
	QVector<UserActionCfg> m_actions;
	QString m_username;
	QString m_host;
	quint16 m_port = 1883;
	QString m_mqttUser;
	QString m_mqttPassword;
	bool m_autoConnect = false;
	bool m_autoReconnect = false;
	int m_reconnectSec = 5;
	QTimer *m_reconnectTimer = nullptr;
	bool m_userInitiatedDisconnect = false;
	bool m_printOnly = false;
};


