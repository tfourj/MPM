#include "ipc_auth.h"
#include "settings.h"

#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QFileInfo>
#include <windows.h>
#include <Aclapi.h>

static QString readAllTrimmed(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
	const QByteArray data = f.readAll();
	return QString::fromUtf8(data).trimmed();
}

QString ipcTokenFilePath()
{
	// Cache to avoid repeated debug logs and recomputation
	static QString s_cachedTokenPath;
	if (!s_cachedTokenPath.isEmpty()) return s_cachedTokenPath;
	// Store token next to settings (now under per-user AppData)
	QString settingsPath = mpmSharedSettingsFilePath();
	QFileInfo fi(settingsPath);
	QDir dir(fi.absolutePath());
	s_cachedTokenPath = dir.filePath("ipc_token");
	qDebug() << "[IPC] Using IPC token path:" << s_cachedTokenPath;
	return s_cachedTokenPath;
}

QString loadOrCreateIpcToken()
{
	const QString path = ipcTokenFilePath();
	QString token = readAllTrimmed(path);
	if (!token.isEmpty()) return token;
	// Generate random 32-byte base64 token
	QByteArray random(32, '\0');
	for (int i = 0; i < random.size(); ++i) random[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
	QString t = random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	QFile f(path);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		f.write(t.toUtf8());
		f.flush();
		f.close();
		// Relax DACL to allow Authenticated Users read/write so GUI/user can access token
		std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
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
	}
	return t;
}


