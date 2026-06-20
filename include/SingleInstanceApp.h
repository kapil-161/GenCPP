#ifndef SINGLEINSTANCEAPP_H
#define SINGLEINSTANCEAPP_H

#include <QApplication>
#include <QFile>
#include <QString>
#include <QDir>
#include <QStandardPaths>

class SingleInstanceApp : public QApplication
{
    Q_OBJECT

public:
    explicit SingleInstanceApp(int &argc, char **argv, const QString &appId = "com.dssat.gen2.app");
    ~SingleInstanceApp();

    bool isFirstInstance() const { return m_isFirstInstance; }
    void showAlreadyRunningMessage();

private:
    bool lockInstance();
    void cleanupLock();

    QString m_appId;
    QString m_lockFilePath;
    QFile *m_lockFile;
    bool m_isFirstInstance;

#ifdef Q_OS_WIN
    void *m_lockHandle; // HANDLE for Windows file locking
#endif
};

#endif // SINGLEINSTANCEAPP_H
