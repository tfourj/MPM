#pragma once

#include <QtGlobal>

#include <windows.h>

// Minimal Windows service wrapper to run a Qt event loop headless.
class MpmWinService {
public:
    static const wchar_t* serviceName();
	static bool install(const wchar_t *serviceName, const wchar_t *displayName, const wchar_t *exePath, DWORD *outError);
	static bool uninstall(const wchar_t *serviceName, DWORD *outError);
	static bool isInstalled(const wchar_t *serviceName);
	static bool isRunning(const wchar_t *serviceName);
	static bool stop(const wchar_t *serviceName, DWORD *outError, DWORD waitTimeoutMs = 10000);
	static bool start(const wchar_t *serviceName, DWORD *outError, DWORD waitTimeoutMs = 10000);
	static int run(int argc, wchar_t *argv[]);

private:
	static void WINAPI ServiceMain(DWORD argc, LPWSTR *argv);
	static void WINAPI ServiceCtrlHandler(DWORD controlCode);
	static void setStatus(DWORD currentState, DWORD win32ExitCode = NO_ERROR, DWORD waitHintMs = 0);
	static SERVICE_STATUS_HANDLE s_statusHandle;
	static SERVICE_STATUS s_status;
	static HANDLE s_stopEvent;
};


