#include "GlueRunner.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

QString GlueRunner::GLUE_DIR;
QString GlueRunner::GLUE_WORK;

// ── resolvePaths ──────────────────────────────────────────────────────────────
bool GlueRunner::resolvePaths(const QString &dssatProPath)
{
    // 1. Try QSettings (user previously set it via menu)
    QSettings s("DSSAT", "GeneticsEditor");
    QString saved = s.value("GlueDir").toString();
    if (!saved.isEmpty() && QFile::exists(saved + "/SimulationControl.csv")) {
        setGlueDir(saved);
        return true;
    }

    // 2. Common install locations
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << "C:/DSSAT48/Tools/GLUE"
               << "C:/glue-GLUEP_software";
#else
    QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    candidates << home + "/glue-GLUEP_software"
               << home + "/GLUEP_software"
               << home + "/GLUE"
               << "/Applications/DSSAT48/TOOLS/GLUE"
               << "/Applications/DSSAT48/Tools/GLUE";
#endif
    for (const QString &c : candidates) {
        if (QFile::exists(c + "/SimulationControl.csv")) {
            setGlueDir(c);
            return true;
        }
    }

    // 3. Parse DGL entry from DSSATPRO file
    if (!dssatProPath.isEmpty()) {
        QFile f(dssatProPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (!line.startsWith("DGL")) continue;
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                // Windows: "DGL C: \DSSAT48\Tools\GLUE ..."
                // macOS:   "DGL // /Applications/DSSAT48/TOOLS/GLUE ..."
                QString dir;
                if (parts.size() >= 3 && parts[1] == "//")
                    dir = parts[2];
                else if (parts.size() >= 2)
                    dir = parts[1] + (parts.size() > 2 && parts[2].startsWith('\\') ? parts[2] : "");
                if (!dir.isEmpty() && QFile::exists(dir + "/SimulationControl.csv")) {
                    setGlueDir(dir);
                    return true;
                }
                break;
            }
        }
    }

    // Not found — set OS defaults so paths are at least sensible
#ifdef Q_OS_WIN
    GLUE_DIR  = "C:/DSSAT48/Tools/GLUE";
    GLUE_WORK = "C:/DSSAT48/GLWork";
#else
    GLUE_DIR  = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/glue-GLUEP_software";
    GLUE_WORK = "/Applications/DSSAT48/GLWORK";
#endif
    return false;
}

// ── setGlueDir ────────────────────────────────────────────────────────────────
void GlueRunner::setGlueDir(const QString &dir)
{
    GLUE_DIR = dir;

    // Read OutputD from SimulationControl.csv to get GLUE_WORK
    QString simCtrl = dir + "/SimulationControl.csv";
    QFile f(simCtrl);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("OutputD,")) {
                GLUE_WORK = line.mid(8).trimmed();
                break;
            }
        }
    }
    // Fallback if OutputD not found in file
    if (GLUE_WORK.isEmpty()) {
#ifdef Q_OS_WIN
        GLUE_WORK = "C:/DSSAT48/GLWork";
#else
        GLUE_WORK = "/Applications/DSSAT48/GLWORK";
#endif
    }

    QSettings("DSSAT", "GeneticsEditor").setValue("GlueDir", dir);
}

// ── unsupportedReason ─────────────────────────────────────────────────────────
QString GlueRunner::unsupportedReason(const CropInfo &cropInfo)
{
    QFile f(GLUE_DIR + "/GlueExcludedModels.csv");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QString model = (cropInfo.modelId.isEmpty() ? cropInfo.module : cropInfo.modelId)
                              .left(5).toUpper();
    for (const QString &line : QString::fromLatin1(f.readAll()).split('\n')) {
        const QStringList c = line.trimmed().split(',');
        if (c.size() < 2 || line.startsWith('*') || line.startsWith('@')) continue;
        if (c[0].trimmed().toUpper() == model
            && c[1].trimmed().compare(cropInfo.cropCode, Qt::CaseInsensitive) == 0)
            return QString("GLUE does not support model %1 for crop %2%3 (listed in "
                           "GlueExcludedModels.csv). Choose another model for this crop.")
                .arg(cropInfo.modelId.isEmpty() ? cropInfo.module : cropInfo.modelId,
                     cropInfo.cropCode,
                     c.size() > 2 && !c[2].trimmed().isEmpty() ? " (" + c[2].trimmed() + ")" : QString());
    }
    return QString();
}

QString GlueRunner::dssatFailure(const QDateTime &since)
{
    QString modelError;
    // Each worker core_<id>/ writes Error_list_<id>.txt when DSSAT fails there;
    // GLUE.r merges them into ErrorFrame.txt only in some runs. Use both.
    const QDir work(GlueRunner::GLUE_WORK);
    const QStringList cores = work.entryList({"core_*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    QString frame;
    QFile ef(work.filePath("ErrorFrame.txt"));
    if (QFileInfo(ef).lastModified() >= since.addSecs(-1) && ef.open(QIODevice::ReadOnly | QIODevice::Text))
        frame = QString::fromLatin1(ef.readAll());
    int failedCores = 0;
    for (const QString &c : cores) {
        bool failed = frame.contains(c + " ") || frame.contains(c + "\n");
        const QDir cd(work.filePath(c));
        for (const QString &f : cd.entryList({"Error_list_*.txt"}, QDir::Files)) {
            QFile lf(cd.filePath(f));
            if (QFileInfo(lf).lastModified() >= since.addSecs(-1)
                && lf.open(QIODevice::ReadOnly | QIODevice::Text)
                && QString::fromLatin1(lf.readAll()).contains("An error occurred"))
                failed = true;
        }
        if (failed) ++failedCores;
    }
    if (!cores.isEmpty() && failedCores == cores.size()) {
        modelError = QString("DSSAT failed in all %1 GLUE workers.").arg(cores.size());
        // Show DSSAT's own message from the first worker's ERROR.OUT
        QFile eo(work.filePath(cores.first() + "/ERROR.OUT"));
        if (eo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QStringList msg;
            for (const QString &l : QString::fromLatin1(eo.readAll()).split('\n'))
                if ((l.contains("Error", Qt::CaseInsensitive) && !l.startsWith("*RUN-TIME"))
                    || l.trimmed().startsWith("File:"))
                    msg << l.trimmed();
            if (!msg.isEmpty()) modelError += "\n" + msg.join("\n");
        }
    }
    return modelError;
}

QString GlueRunner::lastRunWarning()
{
    // GLUE.r reports these in GlueWarning.txt and still exits normally:
    // unsupported model, failed likelihood/posterior ("NA values ..."), etc.
    QFile f(GLUE_WORK + "/GlueWarning.txt");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QStringList msg;
        for (const QString &line : QString::fromLatin1(f.readAll()).split('\n'))
            if (line.contains("not supported", Qt::CaseInsensitive)
                || line.contains("error occurred", Qt::CaseInsensitive)
                || line.contains("failed", Qt::CaseInsensitive))
                msg << line.trimmed();
        if (!msg.isEmpty()) return msg.join('\n');
    }
    // A complete run ends with "The first/second round of GLUE is finished."
    QFile ind(GLUE_WORK + "/ModelRunIndicator.txt");
    if (ind.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromLatin1(ind.readAll()).trimmed().split('\n');
        if (!lines.isEmpty() && !lines.last().contains("GLUE is finished", Qt::CaseInsensitive))
            return "GLUE stopped before finishing (last step: " + lines.last().trimmed() + ")";
    }
    return QString();
}

// ── findRTerm ─────────────────────────────────────────────────────────────────
QString GlueRunner::findRTerm()
{
#ifdef Q_OS_WIN
    QStringList versions = {"R-4.6.0","R-4.5.3","R-4.5.2","R-4.5.1","R-4.4.2","R-4.4.1","R-4.3.3"};
    QStringList bases    = {"C:/Program Files/R", "C:/PROGRA~1/R"};
    for (const QString &base : bases) {
        for (const QString &ver : versions) {
            QString p = base + "/" + ver + "/bin/x64/RTerm.exe";
            if (QFileInfo::exists(p)) return p;
        }
        QDir rBase(base);
        for (const QString &d : rBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString p = base + "/" + d + "/bin/x64/RTerm.exe";
            if (QFileInfo::exists(p)) return p;
        }
    }
    return "RTerm.exe";
#else
    // macOS/Linux: prefer Rscript from common install locations
    QStringList candidates = {
        "/opt/homebrew/bin/Rscript",
        "/usr/local/bin/Rscript",
        "/usr/bin/Rscript"
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }
    return "Rscript";
#endif
}

// ── scanExperiments ───────────────────────────────────────────────────────────
ScanResult GlueRunner::scanExperiments(const CropInfo &cropInfo,
                                       const QString  &cultivarId,
                                       bool includeTreatmentNames)
{
    ScanResult result;

    if (cropInfo.expDir.isEmpty()) {
        result.errorMsg = QString("No experiment directory configured for crop %1")
                              .arg(cropInfo.cropCode);
        return result;
    }

    QString xExt = cropInfo.cropCode + "X";
    QDirIterator it(cropInfo.expDir,
                    QStringList() << "*." + xExt << "*." + xExt.toLower(),
                    QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        result.filesScanned++;

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QStringList allLines;
        QTextStream in(&f);
        while (!in.atEnd()) allLines << in.readLine();
        f.close();

        // ── Step 1: find cultivar number in *CULTIVARS section ────────────────
        int cultivarNum = -1;
        bool inCulSection = false;
        int cuCol = -1, crCol = -1;

        for (const QString &line : allLines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('!')) continue;
            if (trimmed.startsWith("*CULTIVAR")) {
                inCulSection = true; cuCol = -1; continue;
            }
            if (inCulSection) {
                if (trimmed.startsWith('*')) { inCulSection = false; continue; }
                if (trimmed.startsWith("@C")) {
                    QStringList hdrs = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    cuCol = hdrs.indexOf("INGENO");
                    crCol = hdrs.indexOf("CR");
                    continue;
                }
                if (trimmed.startsWith('@') || cuCol < 0) continue;
                QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() <= cuCol) continue;
                if (crCol >= 0 && crCol < parts.size() &&
                    parts[crCol].compare(cropInfo.cropCode, Qt::CaseInsensitive) != 0)
                    continue;
                if (parts[cuCol].compare(cultivarId, Qt::CaseInsensitive) == 0) {
                    cultivarNum = parts[0].toInt();
                    break;
                }
            }
        }
        if (cultivarNum < 0) continue;
        result.filesWithCultivar++;

        // ── Step 2: find matching treatments by character position ────────────
        QList<TreatmentEntry> matchingTreatments;
        bool inTrtSection = false;
        int cuCharPos = -1, tnameStart = -1;

        for (const QString &line : allLines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('!')) continue;
            if (trimmed.startsWith("*TREATMENT")) {
                inTrtSection = true; cuCharPos = -1; tnameStart = -1; continue;
            }
            if (inTrtSection) {
                if (trimmed.startsWith('*')) { inTrtSection = false; continue; }
                if (trimmed.startsWith("@N")) {
                    tnameStart = line.indexOf("TNAME");
                    cuCharPos  = line.indexOf(" CU ");
                    if (cuCharPos >= 0) cuCharPos++;
                    continue;
                }
                if (trimmed.startsWith('@') || cuCharPos < 0) continue;
                if (line.length() <= cuCharPos) continue;
                bool ok;
                int trtNum = line.left(3).trimmed().toInt(&ok);
                if (!ok) continue;
                int cuVal = line.mid(cuCharPos, 3).trimmed().toInt();
                if (cuVal != cultivarNum) continue;

                TreatmentEntry entry;
                entry.number = trtNum;
                if (includeTreatmentNames && tnameStart >= 0 && line.length() > tnameStart)
                    entry.name = line.mid(tnameStart, 25).trimmed();
                if (entry.name.isEmpty())
                    entry.name = QString("Treatment %1").arg(trtNum);
                matchingTreatments << entry;
            }
        }

        if (!matchingTreatments.isEmpty())
            result.treatments[filePath] = matchingTreatments;
    }

    return result;
}

// ── writeBatchFile ────────────────────────────────────────────────────────────
QString GlueRunner::writeBatchFile(const CropInfo &cropInfo,
                                   const QString  &cultivarId,
                                   const QString  &cultivarName,
                                   const TreatmentMap &selected)
{
    QString batchFileName = QString("%1.%2C").arg(cultivarId, cropInfo.cropCode);
    QString batchPath     = GLUE_WORK + "/" + batchFileName;

    QDir().mkpath(GLUE_WORK);
    QFile batchFile(batchPath);
    if (!batchFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();

    QTextStream out(&batchFile);
    QString culName = cultivarName.trimmed().left(16).trimmed();
    out << QString("$BATCH(CULTIVAR):%1%2 %3\n")
           .arg(cropInfo.cropCode, cultivarId, culName);
    out << " \n";
    out << QString("@FILEX%1TRTNO     RP     SQ     OP     CO\n")
           .arg(QString(88, ' '));

    for (auto it = selected.begin(); it != selected.end(); ++it) {
        QString filePath = QDir::toNativeSeparators(it.key());
        for (const TreatmentEntry &e : it.value()) {
            QString padded = filePath.leftJustified(93, ' ');
            out << QString("%1%2      0      0      0      0\n")
                   .arg(padded).arg(e.number, 6);
        }
    }
    batchFile.close();
    return batchPath;
}

// ── updateSimControl ──────────────────────────────────────────────────────────
bool GlueRunner::updateSimControl(const CropInfo &cropInfo,
                                  const QString  &cultivarId,
                                  int runs, int glueFlag,
                                  const QString  &ecoCalib,
                                  int cores)
{
    QString simCtrlPath = GLUE_DIR + "/SimulationControl.csv";
    QFile sc(simCtrlPath);
    if (!sc.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QStringList lines;
    QTextStream in(&sc);
    while (!in.atEnd()) lines << in.readLine();
    sc.close();

    for (QString &line : lines) {
        if (line.startsWith("NumberOfModelRun,"))
            line = QString("NumberOfModelRun,%1").arg(runs);
        else if (line.startsWith("GLUEFlag,"))
            line = QString("GLUEFlag,%1").arg(glueFlag);
        else if (line.startsWith("EcotypeCalibration,"))
            line = QString("EcotypeCalibration,%1").arg(ecoCalib);
        else if (line.startsWith("CultivarBatchFile,"))
            line = QString("CultivarBatchFile,%1.%2C").arg(cultivarId, cropInfo.cropCode);
        else if (line.startsWith("ModelID,"))
            line = QString("ModelID,%1").arg(cropInfo.modelId.isEmpty() ? cropInfo.module : cropInfo.modelId);
    }

    if (cores > 0) {
        bool found = false;
        for (QString &line : lines) {
            if (line.startsWith("Cores,")) {
                line = QString("Cores,%1").arg(cores);
                found = true;
            }
        }
        if (!found) lines << QString("Cores,%1").arg(cores);
    }

    QFile scOut(simCtrlPath);
    if (!scOut.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&scOut);
    for (const QString &l : lines) out << l << "\n";
    return true;
}

// ── Cores ─────────────────────────────────────────────────────────────────────
int GlueRunner::maxCores()
{
    return qMax(1, QThread::idealThreadCount());
}

int GlueRunner::defaultCores()
{
    int saved = QSettings("DSSAT", "GeneticsEditor").value("GlueCores", 0).toInt();
    if (saved > 0) return qMin(saved, maxCores());
    return qMax(1, maxCores() - 1);
}

void GlueRunner::saveCores(int cores)
{
    QSettings("DSSAT", "GeneticsEditor").setValue("GlueCores", cores);
}
