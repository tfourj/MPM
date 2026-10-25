@echo off
setlocal

rem ============================================
rem MPM - Installer Creation Script
rem ============================================
rem Usage: create_installer.bat "path\to\deploy_dir" "version"
rem Based on the Qt Installer Template. Runs unattended from the CMake
rem post build step, so it never pauses.
rem ============================================

rem ============================================
rem CONFIGURATION
rem ============================================
set "APPNAME=MPM"
set "DEFAULT_VERSION=1.0.0"
set "FALLBACK_MAKENSIS_PATH=C:\Program Files (x86)\NSIS\makensis.exe"

rem ============================================
rem Script Logic
rem ============================================

REM === Accept arguments ===
set "APP_BUILD_DIR=%~1"
set "VERSION=%~2"

if not defined APP_BUILD_DIR (
    echo [ERROR] Usage: %~nx0 "path\to\deploy_dir" "version"
    exit /b 1
)

if not defined VERSION set "VERSION=%DEFAULT_VERSION%"

REM === Dynamic Paths ===
set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "NSIS_SCRIPT=%SCRIPT_DIR%installer\installer.nsi"
set "INSTALLER_DIR=%SCRIPT_DIR%temp_installer"
set "APP_COPY_DIR=%INSTALLER_DIR%\%APPNAME%"

echo [INFO] Using version: %VERSION%
echo [INFO] Using NSIS script: %NSIS_SCRIPT%

REM === The installer sets up the service, so both executables are required ===
for %%E in (%APPNAME%.exe MPMService.exe) do (
    if not exist "%APP_BUILD_DIR%\%%E" (
        echo [ERROR] %%E not found in %APP_BUILD_DIR%
        exit /b 1
    )
)

REM === Locate makensis.exe (PATH first, fallback to hardcoded) ===
set "MAKENSIS_PATH="

REM Try PATH
for /f "delims=" %%i in ('where makensis 2^>nul') do (
    set "MAKENSIS_PATH=%%i"
    goto :makensis_found
)

REM Fallback path
if exist "%FALLBACK_MAKENSIS_PATH%" (
    set "MAKENSIS_PATH=%FALLBACK_MAKENSIS_PATH%"
)

:makensis_found
if not defined MAKENSIS_PATH (
    echo [WARN] makensis.exe not found, skipping installer creation
    echo [HINT] Install NSIS and add it to PATH
    exit /b 0
)

REM === Copy new build files ===
echo [INFO] Copying app files to installer folder...
if exist "%INSTALLER_DIR%" rmdir /s /q "%INSTALLER_DIR%"
mkdir "%APP_COPY_DIR%"
xcopy "%APP_BUILD_DIR%\*" "%APP_COPY_DIR%\" /E /I /Y /Q
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to copy files from %APP_BUILD_DIR%
    exit /b 1
)

REM === Copy NSIS script, license and icon to temp directory ===
copy /Y "%NSIS_SCRIPT%" "%INSTALLER_DIR%\installer.nsi" >nul
copy /Y "%PROJECT_DIR%\LICENSE" "%INSTALLER_DIR%\license.txt" >nul
copy /Y "%PROJECT_DIR%\src\assets\mpm.ico" "%INSTALLER_DIR%\mpm.ico" >nul

REM === Generate version.nsh file from VERSION parameter ===
echo [INFO] Generating version.nsh with version: %VERSION%
set "VERSION_NSH_FILE=%INSTALLER_DIR%\version.nsh"

REM Parse version string (e.g., "2.0.0" -> MAJOR=2, MINOR=0, BUILD=0)
for /f "tokens=1,2,3 delims=." %%a in ("%VERSION%") do (
    set "VERSION_MAJOR=%%a"
    set "VERSION_MINOR=%%b"
    set "VERSION_BUILD=%%c"
)

REM Handle cases where minor or build number might be missing
if not defined VERSION_MINOR set "VERSION_MINOR=0"
if not defined VERSION_BUILD set "VERSION_BUILD=0"

echo ; Auto-generated version file > "%VERSION_NSH_FILE%"
echo ; Generated from project VERSION = %VERSION% >> "%VERSION_NSH_FILE%"
echo !define VERSIONMAJOR %VERSION_MAJOR% >> "%VERSION_NSH_FILE%"
echo !define VERSIONMINOR %VERSION_MINOR% >> "%VERSION_NSH_FILE%"
echo !define VERSIONBUILD %VERSION_BUILD% >> "%VERSION_NSH_FILE%"
echo !define FULLVERSION "%VERSION%" >> "%VERSION_NSH_FILE%"

REM === Generate uninstall_files.nsh so the uninstaller removes only installed files ===
set "UNINSTALL_NSH_FILE=%INSTALLER_DIR%\uninstall_files.nsh"
echo ; Auto-generated list of installed files > "%UNINSTALL_NSH_FILE%"
for %%F in ("%APP_COPY_DIR%\*") do echo   Delete "$INSTDIR\%%~nxF" >> "%UNINSTALL_NSH_FILE%"
for /d %%D in ("%APP_COPY_DIR%\*") do echo   RMDir /r "$INSTDIR\%%~nxD" >> "%UNINSTALL_NSH_FILE%"

REM === Compile the NSIS script ===
echo [INFO] Compiling installer with %MAKENSIS_PATH%...
pushd "%INSTALLER_DIR%"
"%MAKENSIS_PATH%" /V2 "installer.nsi"
set "NSIS_RESULT=%ERRORLEVEL%"
popd

REM === Check result ===
if %NSIS_RESULT% NEQ 0 (
    echo [ERROR] NSIS compilation failed!
    exit /b 1
)

REM === Move installer next to the deploy directory ===
set "OUTPUT_INSTALLER=%INSTALLER_DIR%\%APPNAME%-Setup.exe"
set "FINAL_INSTALLER=%APP_BUILD_DIR%\..\%APPNAME%-Setup.exe"

if not exist "%OUTPUT_INSTALLER%" (
    echo [ERROR] Installer file not found at: %OUTPUT_INSTALLER%
    exit /b 1
)
echo [INFO] Moving installer to: %FINAL_INSTALLER%
move /Y "%OUTPUT_INSTALLER%" "%FINAL_INSTALLER%" >nul

REM === Clean up temporary folder ===
echo [INFO] Cleaning up temporary installer folder...
rmdir /s /q "%INSTALLER_DIR%"

if not exist "%FINAL_INSTALLER%" (
    echo [ERROR] Final installer not found!
    exit /b 1
)
echo [SUCCESS] Installer built successfully: %FINAL_INSTALLER%
endlocal
exit /b 0
