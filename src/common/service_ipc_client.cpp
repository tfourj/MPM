#include "service_ipc_client.h"
#include <QDebug>
#include <QSettings>
#include "settings.h"
static int g_lastTransport = 0; // 0=none,1=local,2=tcp
#include "ipc_auth.h"

QByteArray ServiceIpcClient::sendLocal(const QByteArray &cmd, const QString &name, int timeoutMs)
{
	QLocalSocket sock;
	sock.connectToServer(name);
	if (!sock.waitForConnected(timeoutMs)) return QByteArray();
	const QString token = loadOrCreateIpcToken();
	QByteArray payload = token.toUtf8(); payload.append('\n'); payload.append(cmd);
	sock.write(payload);
	sock.flush();
	sock.waitForBytesWritten(timeoutMs);
	if (!sock.waitForReadyRead(timeoutMs)) return QByteArray();
	g_lastTransport = 1;
	return sock.readAll();
}

bool ServiceIpcClient::isAvailable(const QString &name)
{
	Q_UNUSED(name);
	const bool ok = isAvailableLocal();
	if (ok) g_lastTransport = 1;
	qDebug() << "IPC availability (Local-only):" << ok;
	return ok;
}

QByteArray ServiceIpcClient::send(const QByteArray &cmd, const QString &name, int timeoutMs)
{
	QByteArray resp = sendLocal(cmd, name, timeoutMs);
	if (!resp.isEmpty()) {
		qDebug() << "IPC send via Local succeeded for cmd" << cmd;
		g_lastTransport = 1;
		return resp;
	}
	qDebug() << "IPC send failed for all transports for cmd" << cmd;
	return resp;
}

bool ServiceIpcClient::isAvailableLocal(const QString &name)
{
	QLocalSocket sock;
	sock.connectToServer(name);
	return sock.waitForConnected(100);
}

QByteArray ServiceIpcClient::sendPreferred(int preferredOrder, const QByteArray &cmd, const QString &name, int timeoutMs)
{
	Q_UNUSED(preferredOrder);
	return sendLocal(cmd, name, timeoutMs);

}

int ServiceIpcClient::lastTransport()
{
	return g_lastTransport;
}


