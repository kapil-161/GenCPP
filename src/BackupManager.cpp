#include "BackupManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStringList>
#include <algorithm>

QString BackupManager::createBackup(const QString &filePath)
{
    if (!QFile::exists(filePath))
        return QString();

    QFileInfo fi(filePath);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupPath = fi.dir().filePath(
        fi.completeBaseName() + "." + timestamp + ".bak"
    );

    if (QFile::copy(filePath, backupPath))
        return backupPath;

    return QString();
}

void BackupManager::pruneBackups(const QString &filePath, int maxKeep)
{
    QFileInfo fi(filePath);
    QDir dir = fi.dir();
    QString base = fi.completeBaseName();

    // Find all .bak files matching this base
    QStringList filter = { base + ".*.bak" };
    QStringList backups = dir.entryList(filter, QDir::Files, QDir::Name);

    // Delete oldest entries if we exceed maxKeep
    while (backups.size() > maxKeep) {
        QFile::remove(dir.filePath(backups.first()));
        backups.removeFirst();
    }
}
