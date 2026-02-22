#include "DssatProParser.h"
#include "Config.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

// Derive genetics file base name from crop code + module.
// e.g. cropCode="LU", module="CRGRO048" -> "LUGRO048"
QString DssatProParser::buildGeneticsBase(const QString &cropCode, const QString &module)
{
    if (module.length() < 8)
        return QString();
    QString version  = module.right(3);           // "048"
    QString modelTag = module.mid(2, 3);           // "GRO" from CRGRO048
    return cropCode + modelTag + version;          // "LUGRO048"
}

QString DssatProParser::genotypeDir(const QString &dssatProPath)
{
    QFile file(dssatProPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('*') || line.startsWith('!'))
            continue;
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;
        if (parts[0] == "CRD") {
            // Reassemble path: may have a space between drive letter and backslash
            // e.g. "C: \DSSAT48\GENOTYPE" -> "C:\DSSAT48\GENOTYPE"
            QString path = parts[1];
            if (parts.size() >= 3 && !QRegularExpression("^[A-Za-z]:\\\\").match(parts[2]).hasMatch() &&
                parts[2] != "DSCSM048.EXE" && !parts[2].contains(".EXE", Qt::CaseInsensitive)) {
                path += parts[2];
            }
            path.replace(": \\", ":\\").replace(":  \\", ":\\");
            return path;
        }
    }
    return QString();
}

QMap<QString, CropInfo> DssatProParser::discoverCrops(const QString &dssatProPath)
{
    QMap<QString, CropInfo> result;

    // ── Step 1: read DSSATPRO.v48 for directory/exe info ─────────────────────
    QFile proFile(dssatProPath);
    if (!proFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    // Reassemble split paths like ["C:", "\DSSAT48\Maize"] -> "C:\DSSAT48\Maize"
    auto reassemblePath = [](const QStringList &tokens) -> QString {
        if (tokens.isEmpty()) return QString();
        QString path = tokens[0];
        if (tokens.size() > 1 && tokens[1].startsWith('\\'))
            path += tokens[1];
        path.replace(": \\", ":\\").replace(":  \\", ":\\");
        return path;
    };

    QString genoDir;
    QMap<QString, QString> expDirMap;  // cropCode -> experiment dir
    QMap<QString, QString> exeMap;     // cropCode -> exe name

    QTextStream proIn(&proFile);
    while (!proIn.atEnd()) {
        QString trimmed = proIn.readLine().trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('*') || trimmed.startsWith('!'))
            continue;
        QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;
        const QString &key = parts[0];
        const QStringList values = parts.mid(1);

        if (key == "CRD") {
            genoDir = reassemblePath(values);
        } else if (key.length() == 3 && key[0] == 'M' && !key.endsWith('D')) {
            // M{XX} crop model entry — extract exe name
            QString cropCode = key.mid(1);
            for (const QString &v : values)
                if (v.endsWith(".EXE", Qt::CaseInsensitive))
                    exeMap[cropCode] = v;
        } else if (key.length() == 3 && key.endsWith('D') && key != "CRD") {
            // {XX}D experiment directory entry
            expDirMap[key.left(2)] = reassemblePath(values);
        }
    }
    proFile.close();

    if (genoDir.isEmpty()) return result;

    // ── Step 2: parse SIMULATION.CDE for the authoritative crop/model list ───
    QString simCdePath = QFileInfo(dssatProPath).dir().filePath("SIMULATION.CDE");
    QFile simFile(simCdePath);
    if (!simFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    bool inCropModels = false;
    QTextStream simIn(&simFile);
    while (!simIn.atEnd()) {
        QString trimmed = simIn.readLine().trimmed();

        if (trimmed.startsWith("*Simulation/Crop Models")) {
            inCropModels = true;
            continue;
        }
        if (inCropModels && trimmed.startsWith('*'))
            break;  // reached the next section
        if (!inCropModels || trimmed.isEmpty() || trimmed.startsWith('@') || trimmed.startsWith('!'))
            continue;

        // Format: MODEL  CROP  Description words...
        QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;

        QString modelCode = parts[0];  // e.g. "MZCER"
        QString cropCode  = parts[1];  // e.g. "MZ"
        QString descr     = parts.mid(2).join(" ");  // e.g. "CERES-Maize"

        if (modelCode.length() < 5) continue;

        // Genetics base = cropCode + model[2..4] + "048"
        QString base = cropCode + modelCode.mid(2, 3) + "048";  // e.g. "MZCER048"

        QString culPath = genoDir + QDir::separator() + base + ".CUL";
        if (!QFileInfo::exists(culPath)) continue;

        CropInfo info;
        info.module      = base;
        info.exe         = exeMap.value(cropCode, "DSCSM048.EXE");
        info.expDir      = expDirMap.value(cropCode);
        info.culFile     = culPath;
        info.ecoFile     = genoDir + QDir::separator() + base + ".ECO";
        info.speFile     = genoDir + QDir::separator() + base + ".SPE";
        info.cropCode    = cropCode;
        info.description = descr;

        result[base] = info;  // key = "MZCER048" (unique per model/crop pair)
    }
    simFile.close();

    return result;
}
