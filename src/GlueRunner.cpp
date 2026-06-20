#include "GlueRunner.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>

const QString GlueRunner::GLUE_DIR  = "C:/DSSAT48/Tools/GLUE";
const QString GlueRunner::GLUE_WORK = "C:/DSSAT48/GLWork";

// ── findRTerm ─────────────────────────────────────────────────────────────────
QString GlueRunner::findRTerm()
{
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
                                  const QString  &ecoCalib)
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
            line = QString("ModelID,%1").arg(cropInfo.module);
    }

    QFile scOut(simCtrlPath);
    if (!scOut.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&scOut);
    for (const QString &l : lines) out << l << "\n";
    return true;
}
