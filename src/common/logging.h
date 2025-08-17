#ifndef MPM_LOGGING_H
#define MPM_LOGGING_H

#include <QString>

// Installs a Qt message handler that writes logs to the given file.
// Also mirrors logs to stderr for console runs.
// If truncate is true, clears the file at install time.
void initializeFileLogger(const QString &logFilePath, bool truncate = false);

// Optional: capture recent logs in-memory (service can expose them via IPC)
void enableInMemoryLogCapture(int maxLines = 500);
QString takeRecentLogs(); // returns and clears captured logs

#endif // MPM_LOGGING_H


