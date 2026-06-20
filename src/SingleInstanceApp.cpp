#include "SingleInstanceApp.h"
#include <QMessageBox>
#include <QDebug>
#include <QStandardPaths>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/locking.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif

SingleInstanceApp::SingleInstanceApp(int &argc, char **argv, const QString &appId)
    : QApplication(argc, argv)
    , m_appId(appId)
    , m_lockFile(nullptr)
    , m_isFirstInstance(true)
#ifdef Q_OS_WIN
    , m_lockHandle(nullptr)
#endif
{
    m_isFirstInstance = !lockInstance();
}

SingleInstanceApp::~SingleInstanceApp()
{
    cleanupLock();
}

bool SingleInstanceApp::lockInstance()
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty())
        tempDir = QDir::currentPath();
    m_lockFilePath = tempDir + "/Gen2.instance.lock";

    try {
        m_lockFile = new QFile(m_lockFilePath);

        if (!m_lockFile->open(QIODevice::WriteOnly)) {
            qWarning() << "SingleInstanceApp: Could not open lock file:" << m_lockFile->errorString();
            delete m_lockFile;
            m_lockFile = nullptr;
            return false;
        }

#ifdef Q_OS_WIN
        int fileHandle = m_lockFile->handle();
        if (fileHandle == -1) {
            qWarning() << "SingleInstanceApp: Could not get file handle";
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return false;
        }

        if (_locking(fileHandle, _LK_NBLCK, 1) == 0) {
            QString pidString = QString::number(QCoreApplication::applicationPid());
            m_lockFile->write(pidString.toUtf8());
            m_lockFile->flush();
            return false; // no other instance
        } else {
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return true; // another instance running
        }
#else
        int fileHandle = m_lockFile->handle();
        if (fileHandle == -1) {
            qWarning() << "SingleInstanceApp: Could not get file handle";
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return false;
        }

        if (flock(fileHandle, LOCK_EX | LOCK_NB) == 0) {
            QString pidString = QString::number(QCoreApplication::applicationPid());
            m_lockFile->write(pidString.toUtf8());
            m_lockFile->flush();
            return false; // no other instance
        } else {
            m_lockFile->close();
            delete m_lockFile;
            m_lockFile = nullptr;
            return true; // another instance running
        }
#endif

    } catch (const std::exception &e) {
        qCritical() << "SingleInstanceApp: Error:" << e.what();
        return false;
    }
}

void SingleInstanceApp::cleanupLock()
{
    if (m_lockFile) {
#ifdef Q_OS_WIN
        m_lockFile->close();
#else
        int fileHandle = m_lockFile->handle();
        if (fileHandle != -1)
            flock(fileHandle, LOCK_UN);
        m_lockFile->close();
#endif
        delete m_lockFile;
        m_lockFile = nullptr;

        if (QFile::exists(m_lockFilePath))
            QFile::remove(m_lockFilePath);
    }
}

void SingleInstanceApp::showAlreadyRunningMessage()
{
    QMessageBox::critical(
        nullptr,
        "ERROR",
        "Gen2 is already opened.",
        QMessageBox::Ok
    );
}
