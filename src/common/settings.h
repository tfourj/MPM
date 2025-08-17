#ifndef MPM_SETTINGS_H
#define MPM_SETTINGS_H

#include <QString>
#include <QSettings>

// Returns a shared settings file path under ProgramData so both the GUI and
// Windows Service can read the same configuration before user login.
QString mpmSharedSettingsFilePath();

// Convenience to construct QSettings bound to the shared INI file.
inline QSettings mpmCreateSharedSettings()
{
	return QSettings(mpmSharedSettingsFilePath(), QSettings::IniFormat);
}

#endif // MPM_SETTINGS_H


