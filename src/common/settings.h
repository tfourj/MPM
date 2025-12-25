#ifndef MPM_SETTINGS_H
#define MPM_SETTINGS_H

#include <QString>
#include <QSettings>

// Returns a shared settings file path under machine-wide ProgramData only
// (e.g., C:\\ProgramData\\MPM\\MqttPowerManager.ini). Users are granted
// permissions to modify the INI.
QString mpmSharedSettingsFilePath();

// Convenience to construct QSettings bound to the shared INI file.
inline QSettings mpmCreateSharedSettings()
{
	return QSettings(mpmSharedSettingsFilePath(), QSettings::IniFormat);
}

#endif // MPM_SETTINGS_H


