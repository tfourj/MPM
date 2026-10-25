#include "ha_discovery.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMqttClient>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

namespace {

const char *kPublishedKey = "homeassistant/publishedTopics";

// Discovery topic levels and unique ids only allow [a-zA-Z0-9_-]
QString sanitizedId(const QString &s)
{
	static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_-]"));
	QString out = s;
	out.replace(invalid, QStringLiteral("_"));
	return out.toLower();
}

QString iconFor(ActionType type)
{
	switch (type) {
	case ActionType::Shutdown: return QStringLiteral("mdi:power");
	case ActionType::Restart:  return QStringLiteral("mdi:restart");
	case ActionType::Suspend:
	case ActionType::Sleep:    return QStringLiteral("mdi:power-sleep");
	case ActionType::Lock:     return QStringLiteral("mdi:lock");
	case ActionType::OpenExe:  return QStringLiteral("mdi:application-outline");
	}
	return QStringLiteral("mdi:gesture-tap-button");
}

// Shared by every entity so they all land on one device
QJsonObject deviceObject(const QString &username, const QString &deviceName)
{
	QJsonObject device{
		{"identifiers", QJsonArray{QStringLiteral("mpm_%1").arg(sanitizedId(username))}},
		{"name", deviceName.trimmed().isEmpty() ? HaDiscovery::defaultDeviceName(username) : deviceName.trimmed()},
		{"manufacturer", "MPM"},
		{"model", "MQTT Power Manager"},
	};
#ifdef APP_VERSION
	device.insert("sw_version", QString::fromUtf8(APP_VERSION));
#endif
	return device;
}

QJsonObject originObject()
{
	return QJsonObject{{"name", "MPM"}, {"support_url", "https://github.com/tfourj/MPM"}};
}

} // namespace

QString HaDiscovery::statusTopic()
{
	return QStringLiteral("homeassistant/status");
}

QString HaDiscovery::defaultDeviceName(const QString &username)
{
	return QStringLiteral("MPM %1").arg(username);
}

QString HaDiscovery::configTopic(const QString &username, const QString &actionName)
{
	return QStringLiteral("homeassistant/button/mpm_%1/%2/config").arg(sanitizedId(username), sanitizedId(actionName));
}

QByteArray HaDiscovery::configPayload(const QString &username, const QString &deviceName, const Entry &entry)
{
	const QString base = QStringLiteral("mqttpowermanager/%1").arg(username);
	QJsonObject config{
		{"name", entry.name},
		{"unique_id", QStringLiteral("mpm_%1_%2").arg(sanitizedId(username), sanitizedId(entry.name))},
		{"command_topic", base + '/' + entry.name},
		{"payload_press", entry.message},
		{"availability_topic", base + "/health"},
		{"payload_available", "online"},
		{"payload_not_available", "offline"},
		{"icon", iconFor(entry.type)},
		{"device", deviceObject(username, deviceName)},
		{"origin", originObject()},
	};
	if (entry.type == ActionType::Restart) config.insert("device_class", "restart");
	return QJsonDocument(config).toJson(QJsonDocument::Compact);
}

bool HaDiscovery::publish(QMqttClient *client, const QString &username, const QString &deviceName,
                          const QVector<Entry> &entries, QSettings &settings)
{
	if (!client || client->state() != QMqttClient::Connected || username.isEmpty()) return false;
	settings.sync();
	QStringList topics;
	for (const Entry &e : entries) {
		const QString topic = configTopic(username, e.name);
		// Names that differ only by case match the same action, so they share one entity
		if (topics.contains(topic)) continue;
		client->publish(topic, configPayload(username, deviceName, e), 1, true);
		topics << topic;
	}
	// An empty retained config removes the entity in Home Assistant
	const QStringList previous = settings.value(kPublishedKey).toStringList();
	for (const QString &old : previous) {
		if (!old.isEmpty() && !topics.contains(old)) client->publish(old, QByteArray(), 1, true);
	}
	if (topics.isEmpty()) settings.remove(kPublishedKey);
	else settings.setValue(kPublishedKey, topics);
	settings.sync();
	return true;
}

bool HaDiscovery::clear(QMqttClient *client, QSettings &settings)
{
	if (!client || client->state() != QMqttClient::Connected) return false;
	settings.sync();
	const QStringList previous = settings.value(kPublishedKey).toStringList();
	for (const QString &old : previous) {
		if (!old.isEmpty()) client->publish(old, QByteArray(), 1, true);
	}
	settings.remove(kPublishedKey);
	settings.sync();
	return true;
}
