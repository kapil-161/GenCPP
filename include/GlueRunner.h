#ifndef GLUERUNNER_H
#define GLUERUNNER_H

#include <QString>
#include <QDateTime>
#include <QStringList>
#include <QMap>
#include <QList>
#include "DssatProParser.h"

class QProcess;

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

    // Find RTerm.exe / Rscript: the user-selected R directory if set, else auto-detect
    static QString findRTerm();

    // GLUE.r starts parallel R workers that each run DSSAT; QProcess::kill()
    // stops only R itself and leaves those running. Call prepareProcessTree()
    // before start() and killProcessTree() to stop R and everything it started.
    static void prepareProcessTree(QProcess *process);
    static void killProcessTree(QProcess *process);

    // Start R for GLUE with Windows error popups off. R, its workers and DSSAT
    // inherit this, so a DSSAT run killed by Stop while starting exits quietly
    // instead of leaving an "Application Error (0xc0000142)" box behind.
    static void startQuiet(QProcess *process, const QString &program, const QStringList &args);

    // R executable inside an R install folder (R home, its bin, or bin/x64);
    // empty if none. Lets the user pick any of those levels.
    static QString rExecutableIn(const QString &dir);

    // User-selected R directory (QSettings "RDir"); empty = auto-detect.
    static QString rDir();
    static void setRDir(const QString &dir);

    // Why GLUE cannot calibrate this crop/model (empty if it can). GLUE.r lists
    // unsupported pairs in GlueExcludedModels.csv (e.g. PRFRM forage models) and
    // then quits without an error, so check before starting a run.
    static QString unsupportedReason(const CropInfo &cropInfo);

    // Non-empty if DSSAT failed in every GLUE worker (core_*) since `since`:
    // GLUE.r still exits normally then, with a meaningless result.
    static QString dssatFailure(const QDateTime &since);

    // Why the last GLUE.r run did not complete (it exits normally anyway): an
    // error note in GlueWarning.txt, or ModelRunIndicator.txt not reaching
    // "GLUE is finished". Empty if the run completed.
    static QString lastRunWarning();

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
    // cores: CPU cores for GLUE; 0 leaves the existing Cores value unchanged.
    static bool updateSimControl(const CropInfo &cropInfo,
                                 const QString  &cultivarId,
                                 int runs, int glueFlag,
                                 const QString  &ecoCalib,
                                 int cores = 0);

    // Cores available on this machine, and the default to offer (last used, else all but one).
    static int maxCores();
    static int defaultCores();
    static void saveCores(int cores);
};

#endif // GLUERUNNER_H
