#ifndef DSSATPROPARSER_H
#define DSSATPROPARSER_H

#include <QString>
#include <QMap>

struct CropInfo {
    QString module;      // genetics base, e.g. "MZCER048"
    QString exe;         // e.g. "DSCSM048.EXE"
    QString expDir;      // e.g. "C:\DSSAT48\Maize"
    QString culFile;     // full path to .CUL file
    QString ecoFile;     // full path to .ECO file
    QString speFile;     // full path to .SPE file
    QString cropCode;    // 2-char code, e.g. "MZ"
    QString description; // e.g. "CERES-Maize"
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
