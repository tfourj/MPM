#include "settings.h"

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <windows.h>
#include <Aclapi.h>
#include <WtsApi32.h>
#include <ShlObj.h>

QString mpmSharedSettingsFilePath()
{
	// Cache result to avoid repeated Windows API calls and debug spam
	static QString s_cachedPath;
	if (!s_cachedPath.isEmpty()) return s_cachedPath;

	// Determine per-user Roaming AppData path (preferred)
	QString appData;

	// Try to resolve the active interactive user's AppData (useful when running as LocalSystem service)
	HANDLE userToken = nullptr;
	DWORD sessId = WTSGetActiveConsoleSessionId();
	if (sessId != 0xFFFFFFFF && WTSQueryUserToken(sessId, &userToken)) {
		PWSTR wpath = nullptr;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, userToken, &wpath);
		if (SUCCEEDED(hr) && wpath) {
			appData = QString::fromWCharArray(wpath);
			CoTaskMemFree(wpath);
		}
		CloseHandle(userToken);
	}

	// Fallbacks: environment, Qt standard paths, home
	if (appData.isEmpty()) {
		wchar_t buffer[MAX_PATH];
		DWORD size = MAX_PATH;
		if (GetEnvironmentVariableW(L"APPDATA", buffer, size) > 0) {
			appData = QString::fromWCharArray(buffer);
		}
	}
	if (appData.isEmpty())
		appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (appData.isEmpty())
		appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (appData.isEmpty())
		appData = QDir::homePath();
	QDir ad(appData + "/MPM");
	ad.mkpath(".");
	s_cachedPath = ad.filePath("MqttPowerManager.ini");

	qDebug() << "[Settings] Using settings file path:" << s_cachedPath;
	return s_cachedPath;
}


