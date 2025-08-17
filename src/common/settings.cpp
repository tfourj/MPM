#include "settings.h"

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#ifdef _WIN32
#include <windows.h>
#include <Aclapi.h>
#endif

QString mpmSharedSettingsFilePath()
{
	// Prefer ProgramData for system-wide, pre-login availability
	// e.g. C:\ProgramData\MPM\MqttPowerManager.ini
	QString base = QDir::toNativeSeparators("C:/ProgramData/MPM");
	QDir pd(base);
	if (!pd.mkpath(".")) {
		// Fallback to per-user AppData if ProgramData is not writable
		QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (appData.isEmpty()) appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		if (appData.isEmpty()) appData = QDir::homePath();
		QDir ad(appData);
		ad.mkpath("MPM");
		return ad.filePath("MPM/MqttPowerManager.ini");
	}
	const QString filePath = pd.filePath("MqttPowerManager.ini");
	// Ensure file exists
	QFile f(filePath);
	if (!f.exists()) {
		if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
			f.close();
		}
	}
#ifdef _WIN32
	// Relax ACL so Authenticated Users have RW access
	std::wstring wpath = QDir::toNativeSeparators(filePath).toStdWString();
	PSECURITY_DESCRIPTOR pSD = nullptr; PACL pOldDacl = nullptr;
	if (GetNamedSecurityInfoW((LPWSTR)wpath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
							  nullptr, nullptr, &pOldDacl, nullptr, &pSD) == ERROR_SUCCESS) {
		SID_IDENTIFIER_AUTHORITY ntauth = SECURITY_NT_AUTHORITY;
		PSID pAuthUsers = nullptr;
		if (AllocateAndInitializeSid(&ntauth, 1, SECURITY_AUTHENTICATED_USER_RID, 0,0,0,0,0,0,0, &pAuthUsers)) {
			EXPLICIT_ACCESSW ea{};
			ea.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE;
			ea.grfAccessMode = GRANT_ACCESS;
			ea.grfInheritance = NO_INHERITANCE;
			ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
			ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
			ea.Trustee.ptstrName = (LPWSTR)pAuthUsers;
			PACL pNewDacl = nullptr;
			if (SetEntriesInAclW(1, &ea, pOldDacl, &pNewDacl) == ERROR_SUCCESS) {
				SetNamedSecurityInfoW((LPWSTR)wpath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
									  nullptr, nullptr, pNewDacl, nullptr);
				if (pNewDacl) LocalFree(pNewDacl);
			}
			FreeSid(pAuthUsers);
		}
		if (pSD) LocalFree(pSD);
	}
#endif
	return filePath;
}


