#ifndef GLUEQUEUEMANAGER_H
#define GLUEQUEUEMANAGER_H

#include <QObject>
#include <QList>
#include <QProcess>
#include <QTimer>
#include "DssatProParser.h"
#include "GlueRunner.h"

enum class GlueQueueStatus { Pending, Running, Done, Failed };

struct GlueQueueEntry {
    QString        cultivarId;
    QString        cultivarName;
    CropInfo       cropInfo;
    TreatmentMap   selectedTreatments;
    int            runs      = 500;
    int            glueFlag  = 1;   // 1=both, 2=pheno, 3=growth
    QString        ecoCalib  = "N";

    GlueQueueStatus status   = GlueQueueStatus::Pending;
    QString        resultCulLine;
    QString        snapshotDir;  // set after run — GLWork/BackUp/<cropCode>_<cultivarId>/
    QString        errorMsg;
};

class GlueQueueManager : public QObject
{
    Q_OBJECT

public:
    explicit GlueQueueManager(QObject *parent = nullptr);

    void addEntry(const GlueQueueEntry &entry);
    void removeEntry(int index);
    void clearDone();
    const QList<GlueQueueEntry> &entries() const { return m_entries; }
    bool isRunning() const { return m_running; }

signals:
    void queueChanged();
    void entryStarted(int index);
    void entryFinished(int index, bool success, const QString &culLine);
    void progressUpdated(int index, int percent, const QString &label);

public slots:
    void start();
    void stop();

private slots:
    void onGlueOutput();
    void onGlueFinished(int exitCode);
    void onPollProgress();

private:
    void runNext();
    void cleanup();

    QList<GlueQueueEntry> m_entries;
    int       m_currentIndex  = -1;
    bool      m_running       = false;
    QProcess *m_process       = nullptr;
    QTimer   *m_pollTimer     = nullptr;
    int       m_lastLine      = 0;
    int       m_glueRound     = 0;
    QString   m_stderrBuf;
};

#endif // GLUEQUEUEMANAGER_H
