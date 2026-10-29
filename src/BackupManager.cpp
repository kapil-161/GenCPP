#include "BackupManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

QString BackupManager::createBackup(const QString &filePath)
{
    if (!QFile::exists(filePath))
        return QString();

    QFileInfo fi(filePath);
    QDir dir = fi.dir();
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString backupName = fi.fileName() + "." + timestamp + ".bak";
    const QString backupPath = dir.filePath(backupName);

    // A second save within the same second keeps that second's backup
    if (!QFile::exists(backupPath) && !QFile::copy(filePath, backupPath))
        return QString();

    // The folder keeps one backup per file type (CUL, ECO, SPE), whichever crop
    // it came from. Also clear the old "<name>.<timestamp>.bak" style, whose
    // type can't be told from its name.
    const QRegularExpression sameType(
        "^.+\\." + QRegularExpression::escape(fi.suffix()) + "\\.\\d{8}_\\d{6}\\.bak$",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression legacy("^[^.]+\\.\\d{8}_\\d{6}\\.bak$");
    for (const QString &name : dir.entryList({"*.bak"}, QDir::Files)) {
        if (name == backupName) continue;
        if (sameType.match(name).hasMatch() || legacy.match(name).hasMatch())
            QFile::remove(dir.filePath(name));
    }
    return backupPath;
}
