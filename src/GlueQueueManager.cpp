#include "GlueQueueManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QTextStream>

static const QStringList GLUE_PHASES = {
    "Random parameter sets have been generated",
    "Model runs are starting",
    "Likelihood calculation is starting",
    "Likelihood calculation is finished",
    "Starting calculation of posterior",
    "round of GLUE is finished"
};

GlueQueueManager::GlueQueueManager(QObject *parent)
    : QObject(parent) {}

void GlueQueueManager::addEntry(const GlueQueueEntry &entry)
{
    m_entries.append(entry);
    emit queueChanged();
    if (!m_running)
        start();
}

void GlueQueueManager::removeEntry(int index)
{
    if (index < 0 || index >= m_entries.size()) return;
    if (index == m_currentIndex && m_running) return; // can't remove running entry
    m_entries.removeAt(index);
    if (m_currentIndex > index) m_currentIndex--;
    emit queueChanged();
}

void GlueQueueManager::clearDone()
{
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        auto s = m_entries[i].status;
        if (s == GlueQueueStatus::Done || s == GlueQueueStatus::Failed)
            m_entries.removeAt(i);
    }
    emit queueChanged();
}

void GlueQueueManager::start()
{
    if (m_running) return;
    m_running = true;
    runNext();
}

void GlueQueueManager::stop()
{
    m_running = false;
    cleanup();
}

void GlueQueueManager::runNext()
{
    // Find next pending entry
    m_currentIndex = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].status == GlueQueueStatus::Pending) {
            m_currentIndex = i;
            break;
        }
    }

    if (m_currentIndex < 0) {
        m_running = false;
        return;
    }

    GlueQueueEntry &entry = m_entries[m_currentIndex];
    entry.status = GlueQueueStatus::Running;
    emit queueChanged();
    emit entryStarted(m_currentIndex);

    // Write batch file
    QString batchPath = GlueRunner::writeBatchFile(
        entry.cropInfo, entry.cultivarId, entry.cultivarName, entry.selectedTreatments);
    if (batchPath.isEmpty()) {
        entry.status   = GlueQueueStatus::Failed;
        entry.errorMsg = "Failed to write batch file";
        emit queueChanged();
        emit entryFinished(m_currentIndex, false, {});
        runNext();
        return;
    }

    // Update SimulationControl.csv
    if (!GlueRunner::updateSimControl(entry.cropInfo, entry.cultivarId,
                                      entry.runs, entry.glueFlag, entry.ecoCalib)) {
        entry.status   = GlueQueueStatus::Failed;
        entry.errorMsg = "Failed to update SimulationControl.csv";
        emit queueChanged();
        emit entryFinished(m_currentIndex, false, {});
        runNext();
        return;
    }

    // Launch R
    m_lastLine  = 0;
    m_glueRound = 0;
    m_stderrBuf.clear();

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(GlueRunner::GLUE_DIR);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &GlueQueueManager::onGlueOutput);
    connect(m_process, &QProcess::readyReadStandardError,  this, &GlueQueueManager::onGlueOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus){ onGlueFinished(code); });

    QString rterm = GlueRunner::findRTerm();
    m_process->start(rterm, {"--slave", "--file=" + GlueRunner::GLUE_DIR + "/GLUE.r"});

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &GlueQueueManager::onPollProgress);
    m_pollTimer->start(800);

    emit progressUpdated(m_currentIndex, 0, "Starting…");
}

void GlueQueueManager::cleanup()
{
    if (m_pollTimer) { m_pollTimer->stop(); m_pollTimer->deleteLater(); m_pollTimer = nullptr; }
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void GlueQueueManager::onGlueOutput()
{
    if (!m_process) return;
    m_process->readAllStandardOutput();  // drain stdout (DSSAT console noise)
    m_stderrBuf += QString::fromLatin1(m_process->readAllStandardError());
}

void GlueQueueManager::onPollProgress()
{
    QString indFile = GlueRunner::GLUE_WORK + "/ModelRunIndicator.txt";
    QFile fi(indFile);
    if (!fi.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QStringList lines = QString::fromLatin1(fi.readAll()).split('\n');
    fi.close();

    int phaseInRound = 0;
    QString label;

    for (int i = m_lastLine; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("GLUE Flag: 2")) { m_glueRound = 1; phaseInRound = 0; }
        for (int p = 0; p < GLUE_PHASES.size(); ++p) {
            if (line.contains(GLUE_PHASES[p])) { phaseInRound = p + 1; break; }
        }
        if (!line.startsWith("GLUE Flag") && (
            line.startsWith("Random parameter") || line.startsWith("Model runs") ||
            line.startsWith("Likelihood") || line.startsWith("Starting calc") ||
            line.contains("round of GLUE")))
            label = line;
    }
    m_lastLine = lines.size();

    int pct = (int)((m_glueRound * 6 + phaseInRound) / 12.0 * 100);
    if (!label.isEmpty())
        emit progressUpdated(m_currentIndex, qMin(pct, 99),
                             QString("Round %1/2 — %2").arg(m_glueRound + 1).arg(label));
}

void GlueQueueManager::onGlueFinished(int exitCode)
{
    cleanup();

    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size()) return;
    GlueQueueEntry &entry = m_entries[m_currentIndex];

    // GLUE writes <cropCode>GRO048.CUL (full crop file) with the calibrated line updated inside
    QString culLine;
    QString cropCulFile = GlueRunner::GLUE_WORK + "/" + entry.cropInfo.module + ".CUL";
    QFile cf(cropCulFile);
    if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString varId = entry.cultivarId;
        for (const QString &line : QString::fromLatin1(cf.readAll()).split('\n')) {
            if (line.startsWith(varId, Qt::CaseInsensitive)) {
                culLine = line.trimmed();
                break;
            }
        }
        cf.close();
    }

    bool success = (exitCode == 0) && !culLine.isEmpty();
    entry.status        = success ? GlueQueueStatus::Done : GlueQueueStatus::Failed;
    entry.resultCulLine = culLine;
    if (!success) {
        entry.errorMsg = QString("Exit code %1").arg(exitCode);
        if (!m_stderrBuf.isEmpty())
            entry.errorMsg += "\n\n" + m_stderrBuf.trimmed();
    }

    // Save snapshot of all GLWork files to BackUp/<cropCode>_<cultivarId>/
    QString snapDir = GlueRunner::GLUE_WORK + "/BackUp/"
                      + entry.cropInfo.cropCode + "_" + entry.cultivarId;
    QDir().mkpath(snapDir);
    // NoIteratorFlags = no recursion, avoids BackUp/BackUp nesting
    QDirIterator it(GlueRunner::GLUE_WORK, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        QString src = it.next();
        QString dst = snapDir + "/" + QFileInfo(src).fileName();
        QFile::remove(dst);
        QFile::copy(src, dst);
    }
    entry.snapshotDir = snapDir;

    emit queueChanged();
    emit entryFinished(m_currentIndex, success, culLine);

    if (m_running)
        runNext();
}
