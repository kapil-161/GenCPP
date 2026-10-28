#ifndef COMMANDLINEHANDLER_H
#define COMMANDLINEHANDLER_H

#include <QObject>
#include <QStringList>
#include "DssatProParser.h"

struct CommandLineArgs {
    bool isValid     = false;
    bool testMode    = false;   // --test
    bool glueMode    = false;   // --glue
    QString speReportDir;       // --spe-report <dir>
    bool    verbose  = false;   // --verbose: also list each graph built
    QString cropCode;           // e.g. "WH"
    QString cultivarId;         // e.g. "IB0488"
    QString cultivarName;       // e.g. "NEWTON"
    int     runs     = 100;
    int     cores    = 0;       // --cores; 0 = keep SimulationControl.csv value
    QString mode     = "both";  // phenology|growth|both
    QString model;              // e.g. "WHAPS048"; empty = DSSATPRO primary model
};

class CommandLineHandler : public QObject
{
    Q_OBJECT
public:
    explicit CommandLineHandler(QObject *parent = nullptr);

    static CommandLineArgs parseArgs(const QStringList &args);

    // Entry point: inspects args and either runs tests or GLUE headlessly
    // Returns exit code (0 = success, 1 = failure).
    // If not in CLI mode returns -1 (caller should show GUI).
    int run(const QStringList &args);

private:
    int runTests();
    int runGlue(const CommandLineArgs &a);
    int runSpeReport(const QString &dir, bool verbose);

    static void printUsage();
};

#endif // COMMANDLINEHANDLER_H
