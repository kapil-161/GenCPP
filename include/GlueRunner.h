#ifndef GLUERUNNER_H
#define GLUERUNNER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include "DssatProParser.h"

struct TreatmentEntry {
    int    number;
    QString name;   // from TNAME column; may be empty in CLI path
};

// filePath -> list of matching treatments (number + display name)
using TreatmentMap = QMap<QString, QList<TreatmentEntry>>;

struct ScanResult {
    TreatmentMap treatments;
    int filesScanned      = 0;
    int filesWithCultivar = 0;
    QString errorMsg;  // non-empty if fatal error (e.g. no expDir configured)
};

// Pure logic shared between GlueWizard (GUI) and CommandLineHandler (headless)
class GlueRunner
{
public:
    static QString GLUE_DIR;
    static QString GLUE_WORK;

    // Auto-detect GLUE_DIR from QSettings, common locations, or dssatProPath's DGL entry.
    // Call once at startup. Returns true if GLUE_DIR was found.
    static bool resolvePaths(const QString &dssatProPath = QString());

    // Manually set GLUE_DIR (and derive GLUE_WORK from OutputD in SimulationControl.csv).
    // Persists to QSettings.
    static void setGlueDir(const QString &dir);

    // Find RTerm.exe / Rscript on the system
    static QString findRTerm();

    // Scan experiment files for treatments matching the given cultivar.
    // includeTreatmentNames=true reads TNAME column (needed for GUI display).
    static ScanResult scanExperiments(const CropInfo &cropInfo,
                                      const QString  &cultivarId,
                                      bool includeTreatmentNames = true);

    // Write DSSBatch file to GLWork.
    // Returns the path written, or empty on failure.
    static QString writeBatchFile(const CropInfo &cropInfo,
                                  const QString  &cultivarId,
                                  const QString  &cultivarName,
                                  const TreatmentMap &selected);

    // Update SimulationControl.csv with run parameters.
    // glueFlag: 1=both, 2=phenology only, 3=growth parameters
    static bool updateSimControl(const CropInfo &cropInfo,
                                 const QString  &cultivarId,
                                 int runs, int glueFlag,
                                 const QString  &ecoCalib);
};

#endif // GLUERUNNER_H
