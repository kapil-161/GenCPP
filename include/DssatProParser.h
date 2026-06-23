#ifndef DSSATPROPARSER_H
#define DSSATPROPARSER_H

#include <QString>
#include <QMap>

struct CropInfo {
    QString module;      // genetics file base, e.g. "CBGRO048" (used for .CUL/.ECO/.SPE paths)
    QString modelId;     // DSSAT module ID, e.g. "CRGRO048" (written to SimulationControl ModelID)
    QString exe;         // e.g. "DSCSM048.EXE"
    QString expDir;      // e.g. "C:\DSSAT48\Maize"
    QString culFile;     // full path to .CUL file
    QString ecoFile;     // full path to .ECO file
    QString speFile;     // full path to .SPE file
    QString cropCode;    // 2-char code, e.g. "MZ"
    QString description; // e.g. "CERES-Maize"
    bool isPrimary = false; // true if this is the MXX-designated model for the crop
};

class DssatProParser
{
public:
    // Returns map crop_code -> CropInfo for all crops whose .CUL file exists.
    static QMap<QString, CropInfo> discoverCrops(const QString &dssatProPath);

    // Returns the GENOTYPE directory path from DSSATPRO.v48.
    static QString genotypeDir(const QString &dssatProPath);

private:
    static QString buildGeneticsBase(const QString &cropCode, const QString &module);
};

#endif // DSSATPROPARSER_H
