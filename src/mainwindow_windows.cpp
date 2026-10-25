#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QSettings>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include "common/service_ipc_client.h"
#include <windows.h>
#include <winsvc.h>
#include <wtsapi32.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <objbase.h>

void MainWindow::registerPcStateEvents()
{
    m_pcState = PcState::ownSessionState();
    // winId creates the native window, which also gets WM_POWERBROADCAST while hidden in the tray
    WTSRegisterSessionNotification(reinterpret_cast<HWND>(winId()), NOTIFY_FOR_THIS_SESSION);
}

void MainWindow::unregisterPcStateEvents()
{
    WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(winId()));
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    const MSG *msg = static_cast<const MSG *>(message);
    if (eventType == "windows_generic_MSG" && msg) {
        if (msg->message == WM_WTSSESSION_CHANGE) {
            if (msg->wParam == WTS_SESSION_LOCK) setPcState(PcState::State::Locked);
            else if (msg->wParam == WTS_SESSION_UNLOCK) setPcState(PcState::State::Unlocked);
        } else if (msg->message == WM_POWERBROADCAST) {
            if (msg->wParam == PBT_APMSUSPEND) setPcState(PcState::State::Sleep);
            else if (msg->wParam == PBT_APMRESUMEAUTOMATIC) setPcState(PcState::ownSessionState());
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::onStartWithWindowsToggled(bool enabled)
{
    updateWindowsStartup(enabled);
    m_settings.setValue("options/startWithWindows", enabled);
}

void MainWindow::updateWindowsStartup(bool enabled)
{
    QString appName = "MPM";
    QString appPath = QDir::toNativeSeparators(
        (ui->checkBoxLockStartupPath->isChecked() && !ui->lineEditStartupPath->text().trimmed().isEmpty())
            ? ui->lineEditStartupPath->text().trimmed()
            : QCoreApplication::applicationFilePath());
    QSettings runKey("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        runKey.setValue(appName, '"' + appPath + '"');
    } else {
        runKey.remove(appName);
    }
}

void MainWindow::onStartupPathLockToggled(bool locked)
{
    ui->lineEditStartupPath->setEnabled(locked);
    ui->pushButtonBrowseStartupPath->setEnabled(locked);
    saveAllSettingsForce();
    if (ui->checkBoxStartWithWindows->isChecked()) updateWindowsStartup(true);
}

void MainWindow::onBrowseStartupExe()
{
    QString initial = ui->lineEditStartupPath->text().trimmed();
    if (initial.isEmpty()) initial = QCoreApplication::applicationFilePath();
    const QString path = QFileDialog::getOpenFileName(this, "Select executable for startup", QFileInfo(initial).absolutePath(),
                                                      "Executables (*.exe);;All files (*.*)");
    if (!path.isEmpty()) {
        ui->lineEditStartupPath->setText(path);
        saveSettingsIfNeeded();
        if (ui->checkBoxStartWithWindows->isChecked()) updateWindowsStartup(true);
    }
}

void MainWindow::onCreateStartMenuShortcut()
{
    const QString targetPath = (ui->checkBoxLockStartupPath->isChecked() && !ui->lineEditStartupPath->text().trimmed().isEmpty())
                                   ? ui->lineEditStartupPath->text().trimmed()
                                   : QCoreApplication::applicationFilePath();
    const QString nativeTarget = QDir::toNativeSeparators(targetPath);

    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (appsDir.isEmpty()) {
        QMessageBox::warning(this, "Start Menu", "Cannot resolve Start Menu folder.");
        return;
    }
    const QString mpmDir = QDir(appsDir).filePath("MPM");
    QDir().mkpath(mpmDir);
    const QString linkPath = QDir(mpmDir).filePath("MPM.lnk");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(hr);
    IShellLinkW *psl = nullptr;
    HRESULT hrc = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&psl));
    if (SUCCEEDED(hrc) && psl) {
        psl->SetPath((LPCWSTR)nativeTarget.utf16());
        const QString workDir = QFileInfo(nativeTarget).absolutePath();
        psl->SetWorkingDirectory((LPCWSTR)workDir.utf16());
        psl->SetDescription(L"MPM");
        IPersistFile *ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&ppf))) && ppf) {
            const std::wstring wlink = linkPath.toStdWString();
            if (SUCCEEDED(ppf->Save(wlink.c_str(), TRUE))) {
                QMessageBox::information(this, "Start Menu", "Shortcut created in Start Menu.");
            } else {
                QMessageBox::warning(this, "Start Menu", "Failed to save shortcut.");
            }
            ppf->Release();
        } else {
            QMessageBox::warning(this, "Start Menu", "Failed to access IPersistFile.");
        }
        psl->Release();
    } else {
        QMessageBox::warning(this, "Start Menu", "Failed to create IShellLink.");
    }
    if (shouldUninit) CoUninitialize();
}

bool MainWindow::isServiceInstalled() const
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"MPMService", SERVICE_QUERY_STATUS);
    if (svc) { CloseServiceHandle(svc); CloseServiceHandle(scm); return true; }
    CloseServiceHandle(scm);
    return false;
}

bool MainWindow::isServiceRunning() const
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"MPMService", SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS status{};
    BOOL ok = QueryServiceStatus(svc, &status);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (!ok) return false;
    return status.dwCurrentState == SERVICE_RUNNING;
}

bool MainWindow::startService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"MPMService", SERVICE_START);
    if (!svc) { CloseServiceHandle(scm); return false; }
    BOOL ok = StartServiceW(svc, 0, nullptr);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool MainWindow::stopService()
{
    const QByteArray r = ServiceIpcClient::send("shutdown-service");
    if (r == "ok") return true;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"MPMService", SERVICE_STOP);
    if (!svc) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS status{};
    BOOL ok = ControlService(svc, SERVICE_CONTROL_STOP, &status);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}


