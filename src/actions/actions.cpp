#include "actions.h"
#include <QProcess>
#include <QOperatingSystemVersion>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <powrprof.h>
#endif

static bool executeWin(ActionType type, const QString &exePath)
{
#ifdef Q_OS_WIN
    //Enable privilege on the current process
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
    if (type == ActionType::OpenExe) {
        if (exePath.isEmpty()) return false;
        return QProcess::startDetached(exePath);
    }
    if (type == ActionType::Lock) {
        if (LockWorkStation()) return true;
        return QProcess::startDetached("rundll32.exe", {"user32.dll,LockWorkStation"});
    }
#endif
    return false;
}

bool ActionsRegistry::execute(ActionType type, const QString &exePath)
{
    if (executeWin(type, exePath)) return true;

#ifdef Q_OS_LINUX
    if (type == ActionType::Shutdown) {
        return QProcess::startDetached("shutdown", {"now"});
    }
    if (type == ActionType::Restart) {
        return QProcess::startDetached("reboot");
    }
    if (type == ActionType::Suspend || type == ActionType::Sleep) {
        return QProcess::startDetached("systemctl", {"suspend"});
    }
    if (type == ActionType::Lock) {
        // Best effort: try loginctl
        if (QProcess::startDetached("loginctl", {"lock-session"})) return true;
        return QProcess::startDetached("xdg-screensaver", {"lock"});
    }
#endif
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


