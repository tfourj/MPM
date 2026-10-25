#pragma once

#include <QString>
#include <QVector>
#include <QByteArray>
#include "../actions/actions.h"

class QMqttClient;
class QSettings;

// Home Assistant MQTT discovery: each action is announced as a button on one MPM device.
namespace HaDiscovery {
	struct Entry {
		QString name;     // action name, also the MQTT topic suffix
		ActionType type;
		QString message;  // payload the button sends
	};

	// Topic Home Assistant publishes "online" to after it starts
	QString statusTopic();

	QString configTopic(const QString &username, const QString &actionName);
	QByteArray configPayload(const QString &username, const Entry &entry);

	// Publishes retained configs for entries and removes configs of actions deleted since the last sync.
	// Published topics are remembered in settings for that cleanup. Returns false when not connected.
	bool publish(QMqttClient *client, const QString &username, const QVector<Entry> &entries, QSettings &settings);

	// Removes every config published earlier, used when discovery is turned off.
	bool clear(QMqttClient *client, QSettings &settings);
}
