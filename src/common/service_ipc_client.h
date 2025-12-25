#pragma once

#include <QObject>
#include <QLocalSocket>

class ServiceIpcClient : public QObject {
	Q_OBJECT
public:
	static bool isAvailable(const QString &name = QStringLiteral("MPMServiceIpc"));
	static QByteArray send(const QByteArray &cmd, const QString &name = QStringLiteral("MPMServiceIpc"), int timeoutMs = 1000);

	// Transport-specific helpers
	static bool isAvailableLocal(const QString &name = QStringLiteral("MPMServiceIpc"));
	static QByteArray sendLocal(const QByteArray &cmd, const QString &name = QStringLiteral("MPMServiceIpc"), int timeoutMs = 1000);
	// Local-only now
	static QByteArray sendPreferred(int preferredOrder, const QByteArray &cmd, const QString &name = QStringLiteral("MPMServiceIpc"), int timeoutMs = 200);
	static int lastTransport(); // 0=none,1=local
};


