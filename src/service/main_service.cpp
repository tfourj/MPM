#include <QCoreApplication>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include "win_service.h"
#include "mqtt_daemon.h"
#include "../common/settings.h"
#include "../common/logging.h"
#include "ipc_server.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
// Build a console entry that controls install/uninstall and can run as a service

static void printUsage()
{
	QTextStream ts(stdout);
	ts << "Usage:\n";
	ts << "  MPMService.exe --install     Install the Windows Service (auto-start). If already installed, reinstalls.\n";
	ts << "  MPMService.exe --uninstall   Uninstall the Windows Service\n";
	ts << "  MPMService.exe --run         Run in console (debug)\n";
	ts << "  MPMService.exe --reinstall   Stop if running, uninstall, then install again\n";
	ts << "  MPMService.exe --restart     Restart the service if installed\n";
}

static bool isProcessElevated()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	TOKEN_ELEVATION elev{};
	DWORD size = 0;
	BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size);
	CloseHandle(token);
	return ok && elev.TokenIsElevated != 0;
}

static void redirectToPipeIfRequested(const QStringList &args)
{
	int idx = args.indexOf("--pipe");
	if (idx == -1 || idx + 1 >= args.size()) return;
	const QString pipeName = args.at(idx + 1);
	HANDLE hPipe = CreateFileW((LPCWSTR)pipeName.utf16(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hPipe == INVALID_HANDLE_VALUE) return;
	SetStdHandle(STD_OUTPUT_HANDLE, hPipe);
	SetStdHandle(STD_ERROR_HANDLE, hPipe);
	int outFd = _open_osfhandle((intptr_t)hPipe, _O_TEXT);
	if (outFd != -1) {
		_dup2(outFd, _fileno(stdout));
		_dup2(outFd, _fileno(stderr));
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
	}
}

static bool relaunchElevatedIfNeeded(const QStringList &args)
{
	const bool needsAdmin = args.contains("--install") || args.contains("--uninstall") || args.contains("--reinstall") || args.contains("--restart");
	if (!needsAdmin) return false;
	if (isProcessElevated()) return false;
	// Prepare params without argv[0]
	QStringList params = args.mid(1);
	// Create a unique named pipe for log relay
	QString pipeName = QString("\\\\.\\pipe\\MPMServiceCli-%1-%2").arg(GetCurrentProcessId()).arg(GetTickCount());
	params.prepend(pipeName);
	params.prepend("--pipe");
	auto quote = [](const QString &s) -> QString {
		if (s.contains(' ') || s.contains('"')) {
			QString t = s;
			t.replace("\"", "\\\"");
			return QString("\"%1\"").arg(t);
		}
		return s;
	};
	QString paramString;
	for (int i = 0; i < params.size(); ++i) {
		if (i) paramString += ' ';
		paramString += quote(params[i]);
	}
	// Create named pipe server
	HANDLE hPipe = CreateNamedPipeW((LPCWSTR)pipeName.utf16(),
	    PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
	    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
	    1, 4096, 4096, 0, nullptr);
	if (hPipe == INVALID_HANDLE_VALUE) {
		QTextStream(stderr) << "Failed to create relay pipe\n";
		return true; // prevent double execution
	}
	// Launch elevated child
	const QString exePath = QDir::toNativeSeparators(QFileInfo(args.first()).absoluteFilePath());
	QTextStream(stdout) << "Elevating to Administrator..." << "\n";
	HINSTANCE h = ShellExecuteW(nullptr, L"runas",
	                            (LPCWSTR)exePath.utf16(),
	                            (LPCWSTR)paramString.utf16(),
	                            nullptr, SW_SHOWNORMAL);
	if ((INT_PTR)h <= 32) {
		QTextStream(stderr) << "Elevation declined or failed\n";
		CloseHandle(hPipe);
		return true;
	}
	// Relay output from elevated child
	BOOL connected = ConnectNamedPipe(hPipe, nullptr);
	if (!connected) {
		DWORD err = GetLastError();
		if (err != ERROR_PIPE_CONNECTED) {
			QTextStream(stderr) << "Failed to connect relay pipe, err=" << err << "\n";
			CloseHandle(hPipe);
			return true;
		}
	}
	char buffer[1024];
	DWORD readBytes = 0;
	for (;;) {
		BOOL ok = ReadFile(hPipe, buffer, sizeof(buffer), &readBytes, nullptr);
		if (!ok || readBytes == 0) break;
		fwrite(buffer, 1, readBytes, stdout);
		fflush(stdout);
	}
	CloseHandle(hPipe);
	return true;
}

static void attachConsoleIfRequested(const QStringList &args)
{
	if (!args.contains("--attach-console")) return;
	if (GetConsoleWindow() == nullptr) {
		if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
			AllocConsole();
		}
	}
	FILE *fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	freopen_s(&fp, "CONIN$",  "r", stdin);
	SetConsoleOutputCP(CP_UTF8);
}

int main(int argc, char *argv[])
{
	QStringList args;
	for (int i = 0; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);

	// If child was invoked with --pipe, redirect stdout/stderr to the given pipe
	redirectToPipeIfRequested(args);

	// If requested (or on elevated child), attach to parent console for consistent output
	attachConsoleIfRequested(args);

	// Show usage if help requested
	if (args.contains("--help") || args.contains("-h") || args.contains("/?")) {
		printUsage();
		return 0;
	}

	// Relaunch elevated for admin-required operations (and relay output via pipe)
	if (relaunchElevatedIfNeeded(args)) {
		return 0;
	}

	if (args.contains("--install") || args.contains("--reinstall")) {
		const QString exe = QDir::toNativeSeparators(QFileInfo(args.first()).absoluteFilePath());
		// If already installed or explicitly reinstall, stop and uninstall first
		if (MpmWinService::isInstalled(L"MPMService")) {
			DWORD stopErr = 0;
			if (MpmWinService::isRunning(L"MPMService")) {
				MpmWinService::stop(L"MPMService", &stopErr, 15000);
			}
			DWORD unErr = 0;
			MpmWinService::uninstall(L"MPMService", &unErr);
		}
		DWORD err = 0;
		const bool ok = MpmWinService::install(L"MPMService", L"MPM MQTT Power Manager Service", (LPCWSTR)exe.utf16(), &err);
		QTextStream ts(ok ? stdout : stderr);
		if (ok) {
			ts << "Installed service successfully\n";
			DWORD startErr = 0;
			if (MpmWinService::start(L"MPMService", &startErr, 15000)) {
				ts << "Service started\n";
				return 0;
			} else {
				ts << "Service installed but failed to start. WinError=" << startErr << "\n";
				return 1;
			}
		} else {
			ts << "Failed to install service. WinError=" << err << "\n";
			return 1;
		}
	}
	if (args.contains("--uninstall")) {
		DWORD err = 0;
		const bool ok = MpmWinService::uninstall(L"MPMService", &err);
		QTextStream ts(ok ? stdout : stderr);
		if (ok) ts << "Uninstalled service successfully\n";
		else ts << "Failed to uninstall service. WinError=" << err << "\n";
		return ok ? 0 : 1;
	}

	if (args.contains("--restart")) {
		QTextStream ts(stdout);
		if (!MpmWinService::isInstalled(L"MPMService")) {
			ts << "Service is not installed\n";
			return 1;
		}
		DWORD stopErr = 0;
		if (MpmWinService::isRunning(L"MPMService")) {
			MpmWinService::stop(L"MPMService", &stopErr, 15000);
		}
		DWORD startErr = 0;
		if (MpmWinService::start(L"MPMService", &startErr, 15000)) {
			ts << "Service restarted\n";
			return 0;
		} else {
			ts << "Failed to start service. WinError=" << startErr << "\n";
			return 1;
		}
	}

	if (args.contains("--run")) {
		int qtArgc = 1;
		char appName[] = "MPMService";
		char *qtArgv[] = { appName };
		QCoreApplication app(qtArgc, qtArgv);
		initializeFileLogger("C:/ProgramData/MPM/MPMService.log", true);
		enableInMemoryLogCapture(500);
		qInfo() << "MPMService console run starting";
		MqttDaemon daemon;
		IpcServer ipc(&daemon);
		ipc.start();
		QObject::connect(&app, &QCoreApplication::aboutToQuit, &daemon, [&daemon](){ daemon.notifyGoingOffline(); });
		QObject::connect(&app, &QCoreApplication::aboutToQuit, &daemon, [&daemon]() {
			qInfo() << "MPMService stopping";
		});
		daemon.start();
		return app.exec();
	}
	// If no explicit mode, try to run under the Service Control Manager.
	// If not started by SCM, this will fail with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT.
	int rc = MpmWinService::run(0, nullptr);
	if (rc == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
		printUsage();
		return 0;
	}
	return rc;
}


