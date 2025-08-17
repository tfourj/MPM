#include "logging.h"

#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QFileInfo>
#include <QDir>

static QFile g_logFile;
static QMutex g_logMutex;
static QStringList g_recent;
static int g_recentMax = 0;

static void mpmMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
	Q_UNUSED(ctx);
	QString level;
	switch (type) {
	case QtDebugMsg: level = "DEBUG"; break;
	case QtInfoMsg: level = "INFO"; break;
	case QtWarningMsg: level = "WARN"; break;
	case QtCriticalMsg: level = "ERROR"; break;
	case QtFatalMsg: level = "FATAL"; break;
	}
	const QString line = QString("%1 [%2] %3\n")
		.arg(QDateTime::currentDateTime().toString(Qt::ISODate))
		.arg(level)
		.arg(msg);
	{
		QMutexLocker lock(&g_logMutex);
		if (g_logFile.isOpen()) {
			QTextStream ts(&g_logFile);
			ts << line;
			g_logFile.flush();
		}
		if (g_recentMax > 0) {
			g_recent.push_back(line);
			if (g_recent.size() > g_recentMax) g_recent.pop_front();
		}
	}
	QTextStream serr(stderr);
	serr << line;
}

void initializeFileLogger(const QString &logFilePath, bool truncate)
{
	QMutexLocker lock(&g_logMutex);
	if (g_logFile.isOpen()) g_logFile.close();
	// Ensure parent directory exists
	QFileInfo fi(logFilePath);
	QDir dir(fi.absolutePath());
	dir.mkpath(".");
	g_logFile.setFileName(logFilePath);
	if (truncate) {
		g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
		g_logFile.close();
	}
	g_logFile.open(QIODevice::Append | QIODevice::Text);
	qInstallMessageHandler(mpmMessageHandler);
}

void enableInMemoryLogCapture(int maxLines)
{
	QMutexLocker lock(&g_logMutex);
	g_recentMax = qMax(0, maxLines);
	g_recent.clear();
}

QString takeRecentLogs()
{
	QMutexLocker lock(&g_logMutex);
	const QString joined = g_recent.join("");
	g_recent.clear();
	return joined;
}


