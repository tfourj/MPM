#include "actions.h"
#include <QProcess>
#include <QOperatingSystemVersion>
#include <QFileInfo>

#include <windows.h>
#include <powrprof.h>
#include <wtsapi32.h>
#include <userenv.h>
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Userenv.lib")

static bool runInActiveUserSession(const wchar_t *application, const wchar_t *commandLine)
{
	DWORD currentSession = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &currentSession);
	DWORD activeSession = WTSGetActiveConsoleSessionId();
	if (activeSession == 0xFFFFFFFF || activeSession == currentSession) {
		return false; // Not a service context or no active session
	}
	HANDLE userToken = nullptr;
	if (!WTSQueryUserToken(activeSession, &userToken)) return false;
	HANDLE primary = nullptr;
	if (!DuplicateTokenEx(userToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primary)) {
		CloseHandle(userToken);
		return false;
	}
	CloseHandle(userToken);
	void *envBlock = nullptr;
	CreateEnvironmentBlock(&envBlock, primary, FALSE);
	STARTUPINFOW si{}; si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	BOOL ok = CreateProcessAsUserW(primary, application, (LPWSTR)commandLine,
		nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
		envBlock, nullptr, &si, &pi);
	if (ok) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	if (envBlock) DestroyEnvironmentBlock(envBlock);
	CloseHandle(primary);
	return ok;
}

static bool executeWin(ActionType type, const QString &exePath)
{
	// Service-aware handling for session-bound actions
	if (type == ActionType::OpenExe) {
		if (exePath.isEmpty()) return false;
		// Try to run in active user session when running as a service
		const std::wstring wexe = exePath.toStdWString();
		if (runInActiveUserSession(wexe.c_str(), (LPWSTR)wexe.c_str())) return true;
		return QProcess::startDetached(exePath);
	}
	if (type == ActionType::Lock) {
		// Prefer to lock in the active user session
		std::wstring cmd = L"rundll32.exe user32.dll,LockWorkStation";
		if (runInActiveUserSession(L"C:\\Windows\\System32\\rundll32.exe", (LPWSTR)cmd.c_str())) return true;
		if (LockWorkStation()) return true;
		return QProcess::startDetached("rundll32.exe", {"user32.dll,LockWorkStation"});
	}
	// Privileged system power actions can be done from service context
	auto enablePrivilege = [](const wchar_t *privName) -> bool {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
			return false;
		}
		LUID luid{};
		const BOOL okLookup = LookupPrivilegeValueW(nullptr, privName, &luid);
		if (!okLookup) {
			CloseHandle(token);
			return false;
		}
		TOKEN_PRIVILEGES tp{};
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		const BOOL okAdjust = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
		const DWORD err = GetLastError();
		CloseHandle(token);
		return okAdjust && (err == ERROR_SUCCESS);
	};

	if (type == ActionType::Shutdown) {
		// Try with proper privilege otherwise fall back to shutdown command
		enablePrivilege(SE_SHUTDOWN_NAME);
		if (ExitWindowsEx(EWX_POWEROFF | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED)) {
			return true;
		}
		return QProcess::startDetached("shutdown", {"/s", "/t", "0"});
	}
	if (type == ActionType::Restart) {
		enablePrivilege(SE_SHUTDOWN_NAME);
		if (ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED)) {
			return true;
		}
		return QProcess::startDetached("shutdown", {"/r", "/t", "0"});
	}
	if (type == ActionType::Suspend || type == ActionType::Sleep) {
		enablePrivilege(SE_SHUTDOWN_NAME);
		if (SetSuspendState(FALSE, TRUE, FALSE)) {
			return true;
		}
		return QProcess::startDetached("rundll32.exe", {"powrprof.dll,SetSuspendState", "0", "1", "0"});
	}
	return false;
}

bool ActionsRegistry::execute(ActionType type, const QString &exePath)
{
	if (executeWin(type, exePath)) return true;
	return false;
}

QString ActionsRegistry::toString(ActionType type)
{
	switch (type) {
	case ActionType::Shutdown: return QStringLiteral("Shutdown");
	case ActionType::Restart:  return QStringLiteral("Restart");
	case ActionType::Suspend:  return QStringLiteral("Suspend");
	case ActionType::Sleep:    return QStringLiteral("Sleep");
	case ActionType::OpenExe:  return QStringLiteral("OpenExe");
	case ActionType::Lock:     return QStringLiteral("Lock");
	}
	return QStringLiteral("Shutdown");
}

bool ActionsRegistry::fromString(const QString &str, ActionType &outType)
{
	const QString s = str.trimmed();
	if (s.compare("Shutdown", Qt::CaseInsensitive) == 0) { outType = ActionType::Shutdown; return true; }
	if (s.compare("Restart", Qt::CaseInsensitive) == 0)  { outType = ActionType::Restart;  return true; }
	if (s.compare("Suspend", Qt::CaseInsensitive) == 0)  { outType = ActionType::Suspend;  return true; }
	if (s.compare("Sleep", Qt::CaseInsensitive) == 0)    { outType = ActionType::Sleep;    return true; }
	if (s.compare("OpenExe", Qt::CaseInsensitive) == 0)  { outType = ActionType::OpenExe;  return true; }
	if (s.compare("Lock", Qt::CaseInsensitive) == 0)     { outType = ActionType::Lock;     return true; }
	return false;
}


