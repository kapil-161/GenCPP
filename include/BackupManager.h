#ifndef BACKUPMANAGER_H
#define BACKUPMANAGER_H

#include <QString>

class BackupManager
{
public:
    // Create a timestamped backup of filePath next to the original file.
    // Returns the backup path, or empty string on failure.
    static QString createBackup(const QString &filePath);

    // Remove oldest backup files, keeping only maxKeep.
    static void pruneBackups(const QString &filePath, int maxKeep = 10);
};

#endif // BACKUPMANAGER_H
