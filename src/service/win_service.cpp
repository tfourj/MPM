#include "win_service.h"

#include <QCoreApplication>
#include <QTimer>
#include "mqtt_daemon.h"
#include "../common/logging.h"
#include "ipc_server.h"
#include <Aclapi.h>
#include <string>

SERVICE_STATUS_HANDLE MpmWinService::s_statusHandle = nullptr;
SERVICE_STATUS MpmWinService::s_status = {};
HANDLE MpmWinService::s_stopEvent = nullptr;

static const wchar_t *kServiceName = L"MPMService";

const wchar_t* MpmWinService::serviceName() { return kServiceName; }

bool MpmWinService::install(const wchar_t *serviceName, const wchar_t *displayName, const wchar_t *exePath, DWORD *outError)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) { if (outError) *outError = GetLastError(); return false; }
    // Quote the binary path in case it contains spaces
    std::wstring binPath; binPath.reserve(wcslen(exePath) + 2);
    binPath.push_back(L'"'); binPath.append(exePath); binPath.push_back(L'"');
    SC_HANDLE svc = CreateServiceW(
        scm,
        serviceName,
        displayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        nullptr, nullptr, L"", nullptr, nullptr);
    if (!svc) { if (outError) *outError = GetLastError(); CloseServiceHandle(scm); return false; }

    // Relax service DACL to allow non-admin users to start/stop/query
    // Grant SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS to Authenticated Users
    PSECURITY_DESCRIPTOR pSD = nullptr;
    DWORD needed = 0;
    QueryServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
    pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, needed);
    if (pSD && QueryServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, pSD, needed, &needed)) {
        PACL pOldDacl = nullptr;
        BOOL daclPresent = FALSE, daclDefaulted = FALSE;
        if (GetSecurityDescriptorDacl(pSD, &daclPresent, &pOldDacl, &daclDefaulted)) {
            SID_IDENTIFIER_AUTHORITY ntauth = SECURITY_NT_AUTHORITY;
            PSID pAuthUsers = nullptr;
            if (AllocateAndInitializeSid(&ntauth, 1, SECURITY_AUTHENTICATED_USER_RID, 0,0,0,0,0,0,0, &pAuthUsers)) {
                EXPLICIT_ACCESSW ea{};
                ea.grfAccessPermissions = SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS;
                ea.grfAccessMode = GRANT_ACCESS;
                ea.grfInheritance = NO_INHERITANCE;
                ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
                ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
                ea.Trustee.ptstrName = (LPWSTR)pAuthUsers;
                PACL pNewDacl = nullptr;
                if (SetEntriesInAclW(1, &ea, pOldDacl, &pNewDacl) == ERROR_SUCCESS) {
                    SECURITY_DESCRIPTOR sdNew{};
                    if (InitializeSecurityDescriptor(&sdNew, SECURITY_DESCRIPTOR_REVISION)) {
                        if (SetSecurityDescriptorDacl(&sdNew, TRUE, pNewDacl, FALSE)) {
                            SetServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, &sdNew);
                        }
                    }
                    if (pNewDacl) LocalFree(pNewDacl);
                }
                FreeSid(pAuthUsers);
            }
        }
    }
    if (pSD) LocalFree(pSD);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (outError) *outError = 0;
    return true;
}

bool MpmWinService::uninstall(const wchar_t *serviceName, DWORD *outError)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { if (outError) *outError = GetLastError(); return false; }
    SC_HANDLE svc = OpenServiceW(scm, serviceName, DELETE);
    if (!svc) { if (outError) *outError = GetLastError(); CloseServiceHandle(scm); return false; }
    BOOL ok = DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (!ok) { if (outError) *outError = GetLastError(); return false; }
    if (outError) *outError = 0;
    return true;
}

bool MpmWinService::isInstalled(const wchar_t *serviceName)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool MpmWinService::isRunning(const wchar_t *serviceName)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS status{};
    BOOL ok = QueryServiceStatus(svc, &status);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (!ok) return false;
    return status.dwCurrentState == SERVICE_RUNNING || status.dwCurrentState == SERVICE_START_PENDING;
}

bool MpmWinService::stop(const wchar_t *serviceName, DWORD *outError, DWORD waitTimeoutMs)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { if (outError) *outError = GetLastError(); return false; }
    SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) { if (outError) *outError = GetLastError(); CloseServiceHandle(scm); return false; }
    SERVICE_STATUS status{};
    BOOL sent = ControlService(svc, SERVICE_CONTROL_STOP, &status);
    if (!sent) { if (outError) *outError = GetLastError(); CloseServiceHandle(svc); CloseServiceHandle(scm); return false; }
    // Poll until stopped or timeout
    const DWORD startTick = GetTickCount();
    for (;;) {
        Sleep(200);
        SERVICE_STATUS s{};
        if (!QueryServiceStatus(svc, &s)) break;
        if (s.dwCurrentState == SERVICE_STOPPED) {
            if (outError) *outError = 0; CloseServiceHandle(svc); CloseServiceHandle(scm); return true;
        }
        if (GetTickCount() - startTick > waitTimeoutMs) break;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (outError) *outError = ERROR_TIMEOUT;
    return false;
}

bool MpmWinService::start(const wchar_t *serviceName, DWORD *outError, DWORD waitTimeoutMs)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { if (outError) *outError = GetLastError(); return false; }
    SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) { if (outError) *outError = GetLastError(); CloseServiceHandle(scm); return false; }
    BOOL ok = StartServiceW(svc, 0, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        if (outError) *outError = err;
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return false;
    }
    const DWORD startTick = GetTickCount();
    for (;;) {
        Sleep(200);
        SERVICE_STATUS s{};
        if (!QueryServiceStatus(svc, &s)) break;
        if (s.dwCurrentState == SERVICE_RUNNING) {
            if (outError) *outError = 0; CloseServiceHandle(svc); CloseServiceHandle(scm); return true;
        }
        if (GetTickCount() - startTick > waitTimeoutMs) break;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (outError) *outError = ERROR_TIMEOUT;
    return false;
}

int MpmWinService::run(int argc, wchar_t *argv[])
{
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr }
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        return GetLastError();
    }
    return 0;
}

void WINAPI MpmWinService::ServiceMain(DWORD, LPWSTR*)
{
    s_statusHandle = RegisterServiceCtrlHandlerW(kServiceName, ServiceCtrlHandler);
    if (!s_statusHandle) return;

    ZeroMemory(&s_status, sizeof(s_status));
    s_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    setStatus(SERVICE_START_PENDING);

    s_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_stopEvent) {
        setStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    setStatus(SERVICE_RUNNING);

    int fakeArgc = 0;
    char appName[] = "MPMService";
    char *fakeArgv[] = { appName };
    int statusCode = 0;
    {
        int qtArgc = 1;
        char **qtArgv = fakeArgv;
        QCoreApplication app(qtArgc, qtArgv);
        initializeFileLogger("C:/ProgramData/MPM/MPMService.log", true);
        enableInMemoryLogCapture(500);
        qInfo() << "MPMService started (v1.0.0)";
        MqttDaemon daemon;
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &daemon, [&daemon](){ daemon.notifyGoingOffline(); });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &daemon, [&daemon](){ daemon.notifyGoingOffline(); });
        daemon.start();
        IpcServer ipc(&daemon);
        ipc.start();
        // Quit when stop event is signaled
        QTimer poll;
        poll.setInterval(200);
        QObject::connect(&poll, &QTimer::timeout, [&app]() {
            if (WaitForSingleObject(MpmWinService::s_stopEvent, 0) == WAIT_OBJECT_0) {
                app.quit();
            }
        });
        poll.start();
        statusCode = app.exec();
    }

    if (s_stopEvent) CloseHandle(s_stopEvent), s_stopEvent = nullptr;
    setStatus(SERVICE_STOPPED);
}

void WINAPI MpmWinService::ServiceCtrlHandler(DWORD controlCode)
{
    if (controlCode == SERVICE_CONTROL_STOP || controlCode == SERVICE_CONTROL_SHUTDOWN) {
        setStatus(SERVICE_STOP_PENDING);
        if (s_stopEvent) SetEvent(s_stopEvent);
    }
}

void MpmWinService::setStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHintMs)
{
    s_status.dwCurrentState = currentState;
    s_status.dwWin32ExitCode = win32ExitCode;
    s_status.dwWaitHint = waitHintMs;
    s_status.dwControlsAccepted = (currentState == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    if (s_statusHandle) SetServiceStatus(s_statusHandle, &s_status);
}

 


