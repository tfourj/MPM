#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include "mqtt_daemon.h"

class IpcServer : public QObject {
	Q_OBJECT
public:
	explicit IpcServer(MqttDaemon *daemon, QObject *parent = nullptr);
	bool start(const QString &serverName = QStringLiteral("MPMServiceIpc"));

private slots:
	void onNewConnection();
	void handleSocket(QLocalSocket *sock);

private:
	MqttDaemon *m_daemon;
	QLocalServer m_server;
};


