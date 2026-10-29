#ifndef BACKUPMANAGER_H
#define BACKUPMANAGER_H

#include <QString>

class BackupManager
{
public:
    // Create a timestamped backup of filePath next to the original file and
    // remove the folder's other backups of the same file type (CUL/ECO/SPE).
    // Returns the backup path, or empty string on failure.
    static QString createBackup(const QString &filePath);
};

#endif // BACKUPMANAGER_H
