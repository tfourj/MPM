#include "ipc_server.h"
#include <QTextStream>
#include <QCoreApplication>
#include "../common/settings.h"
#include "../common/ipc_auth.h"
#include "../common/logging.h"

IpcServer::IpcServer(MqttDaemon *daemon, QObject *parent)
    : QObject(parent), m_daemon(daemon)
{
}

bool IpcServer::start(const QString &serverName)
{
    QLocalServer::removeServer(serverName);
    // Allow cross-user access so GUI (user) can reach service (LocalSystem)
    m_server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_server.listen(serverName)) {
        qWarning() << "IPC Local listen failed for" << serverName << ":" << m_server.errorString();
    } else {
        qInfo() << "IPC Local listening at" << serverName;
        connect(&m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
    }
    return true;
}

void IpcServer::onNewConnection()
{
    while (QLocalSocket *sock = m_server.nextPendingConnection()) {
        //qDebug() << "IPC client connected via Local";
        handleSocket(sock);
    }
}

void IpcServer::handleSocket(QLocalSocket *sock)
{
    connect(sock, &QLocalSocket::disconnected, sock, &QLocalSocket::deleteLater);
    if (!sock->waitForReadyRead(1000)) { sock->disconnectFromServer(); return; }
    const QByteArray req = sock->readAll();
    QByteArray resp;
    const QString token = loadOrCreateIpcToken();
    // Expect commands in form: TOKEN\nCMD
    QList<QByteArray> parts = req.split('\n');
    if (parts.size() < 2 || QString::fromUtf8(parts[0]).trimmed() != token) {
        qWarning() << "IPC unauthorized Local request";
        sock->write("unauthorized"); sock->flush(); sock->waitForBytesWritten(200); sock->disconnectFromServer(); return;
    }
    const QByteArray cmd = parts.mid(1).join("\n");
    //qDebug() << "IPC Local command:" << cmd;
    if (cmd == "status") {
        const auto state = m_daemon ? m_daemon->state() : QMqttClient::Disconnected;
        resp = QByteArray::number(static_cast<int>(state));
    } else if (cmd == "getlogs") {
        resp = takeRecentLogs().toUtf8();
    } else if (cmd == "reload-settings") {
        if (m_daemon) m_daemon->reloadSettings();
        resp = "ok";
    } else if (cmd == "connect") {
        if (m_daemon) m_daemon->forceConnect();
        resp = "ok";
    } else if (cmd == "disconnect") {
        if (m_daemon) { m_daemon->forceDisconnect(); }
        // Mark user-initiated so auto-reconnect pauses until next connect
        if (m_daemon) {
            // best-effort via reloadSettings flipping flag; handled in daemon
        }
        resp = "ok";
    } else if (cmd == "shutdown-service") {
        // Request the service process to exit
        QCoreApplication::quit();
        resp = "ok";
    } else {
        resp = "err";
    }
    sock->write(resp);
    sock->flush();
    sock->waitForBytesWritten(500);
    sock->disconnectFromServer();
}

// No TCP handler anymore; IPC is local-only


