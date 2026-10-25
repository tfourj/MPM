@echo off
setlocal

rem ============================================
rem MPM - Post Deployment Script
rem ============================================
rem Usage: user_post_deploy.bat "path\to\deploy_dir" "version"
rem Called by the CMake post build step after windeployqt.
rem Creates an optional ZIP (needs WinRAR) and the NSIS installer.
rem ============================================

set "APPNAME=MPM"
set "DEFAULT_VERSION=1.0.0"

set "DEPLOY_DIR=%~1"
set "VERSION=%~2"
if not defined DEPLOY_DIR (
  echo Usage: %~nx0 path\to\deploy_dir version
  exit /b 1
)

if not defined VERSION set "VERSION=%DEFAULT_VERSION%"

rem ============================================
rem Locate WinRAR (optional)
rem ============================================

set "WINRAR_EXE="

rem 1) Try PATH
for %%R in (WinRAR.exe) do (
  where %%R >nul 2>&1 && set "WINRAR_EXE=%%~$PATH:R"
)

rem 2) Fallback to default install path
if not defined WINRAR_EXE if exist "C:\Program Files\WinRAR\WinRAR.exe" (
  set "WINRAR_EXE=C:\Program Files\WinRAR\WinRAR.exe"
)

rem ============================================
rem ZIP creation
rem ============================================

if not defined WINRAR_EXE (
  echo [INFO] WinRAR.exe not found, skipping ZIP creation
  goto :installer
)

set "OUTPUT_ZIP=%DEPLOY_DIR%\..\%APPNAME%.zip"
echo [INFO] Repacking "%DEPLOY_DIR%" into "%OUTPUT_ZIP%" with folder "%APPNAME%"...
if exist "%OUTPUT_ZIP%" del /q "%OUTPUT_ZIP%"

pushd "%DEPLOY_DIR%" || (
  echo [ERROR] Failed to cd "%DEPLOY_DIR%"
  exit /b 1
)
"%WINRAR_EXE%" a -afzip -r "%OUTPUT_ZIP%" * -ap%APPNAME%\
popd

rem ============================================
rem Call installer
rem ============================================

:installer
set "SCRIPT_DIR=%~dp0"
echo [INFO] Calling installer creation script...
call "%SCRIPT_DIR%create_installer.bat" "%DEPLOY_DIR%" "%VERSION%"
if errorlevel 1 exit /b 1

endlocal
exit /b 0
