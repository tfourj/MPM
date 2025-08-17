#pragma once

#include <QString>

// Returns the path to the IPC token file under ProgramData.
QString ipcTokenFilePath();

// Loads the IPC token if present, otherwise creates a new random token and saves it.
QString loadOrCreateIpcToken();


