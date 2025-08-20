#include "settings.h"

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <windows.h>
#include <Aclapi.h>
#include <AccCtrl.h>
#include <WtsApi32.h>
#include <ShlObj.h>

QString mpmSharedSettingsFilePath()
{
	// Cache result to avoid repeated Windows API calls and debug spam
	static QString s_cachedPath;
	if (!s_cachedPath.isEmpty()) return s_cachedPath;

	// Helper to grant modify rights to Authenticated Users on a file or directory
	auto ensureWritableByAuthenticatedUsers = [](const QString &path, bool isDirectory) {
		PSID sid = nullptr;
		PACL pOldDACL = nullptr;
		PACL pNewDACL = nullptr;
		PSECURITY_DESCRIPTOR pSD = nullptr;

		BYTE sidBuffer[SECURITY_MAX_SID_SIZE];
		DWORD sidSize = sizeof(sidBuffer);
		if (!CreateWellKnownSid(WinAuthenticatedUserSid, nullptr, sidBuffer, &sidSize)) {
			return; // best-effort
		}
		sid = (PSID)sidBuffer;

		EXPLICIT_ACCESSW ea{};
		ea.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
		ea.grfAccessMode = GRANT_ACCESS;
		ea.grfInheritance = isDirectory ? (SUB_CONTAINERS_AND_OBJECTS_INHERIT) : NO_INHERITANCE;
		ea.Trustee.pMultipleTrustee = nullptr;
		ea.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
		ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
		ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
		ea.Trustee.ptstrName = (LPWSTR)sid;

		std::wstring wpath = path.toStdWString();
		if (GetNamedSecurityInfoW(wpath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &pOldDACL, nullptr, &pSD) != ERROR_SUCCESS) {
			if (pSD) LocalFree(pSD);
			return;
		}
		if (SetEntriesInAclW(1, &ea, pOldDACL, &pNewDACL) != ERROR_SUCCESS) {
			if (pSD) LocalFree(pSD);
			return;
		}
		SetNamedSecurityInfoW((LPWSTR)wpath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, pNewDACL, nullptr);
		if (pNewDACL) LocalFree(pNewDACL);
		if (pSD) LocalFree(pSD);
	};

	// Resolve ProgramData
	QString programData;
	{
		PWSTR wpath = nullptr;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &wpath);
		if (SUCCEEDED(hr) && wpath) {
			programData = QString::fromWCharArray(wpath);
			CoTaskMemFree(wpath);
		}
	}
	if (programData.isEmpty()) {
		wchar_t buffer[MAX_PATH];
		DWORD size = MAX_PATH;
		if (GetEnvironmentVariableW(L"PROGRAMDATA", buffer, size) > 0) {
			programData = QString::fromWCharArray(buffer);
		}
	}
	if (programData.isEmpty()) {
		programData = QString::fromWCharArray(L"C:/ProgramData");
	}

	QDir pd(programData + "/MPM");
	pd.mkpath(".");
	ensureWritableByAuthenticatedUsers(pd.path(), true);

	const QString targetPath = pd.filePath("MqttPowerManager.ini");
	if (!QFile::exists(targetPath)) {
		QFile f(targetPath);
		if (f.open(QIODevice::WriteOnly)) {
			f.write("");
			f.close();
		}
	}
	ensureWritableByAuthenticatedUsers(targetPath, false);

	s_cachedPath = targetPath;

	qDebug() << "[Settings] Using settings file path:" << s_cachedPath;
	return s_cachedPath;
}


