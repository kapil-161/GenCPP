#include "CommandLineHandler.h"
#include "CulParser.h"
#include "EcoParser.h"
#include "DssatProParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QProcess>
#include <QDebug>

#include <cstdio>

// ── tiny test harness ─────────────────────────────────────────────────────────

static int s_pass = 0, s_fail = 0;

static void check(bool ok, const char *desc)
{
    if (ok) {
        fprintf(stdout, "  PASS  %s\n", desc);
        ++s_pass;
    } else {
        fprintf(stderr, "  FAIL  %s\n", desc);
        ++s_fail;
    }
    fflush(stdout); fflush(stderr);
}

// ── CommandLineHandler ────────────────────────────────────────────────────────

CommandLineHandler::CommandLineHandler(QObject *parent) : QObject(parent) {}

CommandLineArgs CommandLineHandler::parseArgs(const QStringList &args)
{
    CommandLineArgs r;
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == "--test") {
            r.testMode = true;
            r.isValid  = true;
        } else if (a == "--glue") {
            r.glueMode = true;
            r.isValid  = true;
        } else if (a == "--crop" && i+1 < args.size()) {
            r.cropCode = args[++i].toUpper();
        } else if (a == "--cultivar" && i+1 < args.size()) {
            r.cultivarId = args[++i];
        } else if (a == "--name" && i+1 < args.size()) {
            r.cultivarName = args[++i];
        } else if (a == "--runs" && i+1 < args.size()) {
            r.runs = args[++i].toInt();
        } else if (a == "--mode" && i+1 < args.size()) {
            r.mode = args[++i].toLower();
        }
    }
    return r;
}

int CommandLineHandler::run(const QStringList &args)
{
    CommandLineArgs a = parseArgs(args);
    if (!a.isValid) return -1; // no CLI flags — show GUI

    if (a.testMode) return runTests();
    if (a.glueMode) return runGlue(a);
    return -1;
}

// ── Test suite ────────────────────────────────────────────────────────────────

int CommandLineHandler::runTests()
{
    fprintf(stdout, "\n=== Gen2 Headless Test Suite ===\n\n");
    fflush(stdout);

    const QString GENOTYPE = "C:/DSSAT48/Genotype";
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        fprintf(stderr, "Cannot create temp dir\n");
        return 1;
    }

    // ── 1. CulParser: dollar-sign header preserved ────────────────────────────
    fprintf(stdout, "[ CulParser: header lines ]\n");
    {
        QString src = GENOTYPE + "/WHCER048.CUL";
        QStringList hdr;
        QVector<CulRow> rows = CulParser::parse(src, hdr);
        check(!rows.isEmpty(), "WHCER048.CUL parsed non-empty");
        bool hasDollar = false;
        for (const QString &h : hdr) if (h.startsWith('$')) { hasDollar = true; break; }
        check(hasDollar, "$ version line preserved in headerLines");
    }

    // ── 2. CulParser: round-trip write/parse preserves $ header ──────────────
    fprintf(stdout, "\n[ CulParser: round-trip write/parse ]\n");
    {
        QString src = GENOTYPE + "/WHCER048.CUL";
        QStringList hdr;
        QVector<CulRow> rows = CulParser::parse(src, hdr);
        QStringList paramNames = CulParser::extractParamNames(hdr);

        QString dst = tmp.filePath("WHCER048_rt.CUL");
        bool wrote = CulParser::write(dst, rows, hdr, paramNames);
        check(wrote, "write() returned true");

        QStringList hdr2;
        QVector<CulRow> rows2 = CulParser::parse(dst, hdr2);
        check(rows2.size() == rows.size(),
              qPrintable(QString("row count preserved (%1)").arg(rows.size())));

        bool hasDollar2 = false;
        for (const QString &h : hdr2) if (h.startsWith('$')) { hasDollar2 = true; break; }
        check(hasDollar2, "$ version line survives write+parse");
    }

    // ── 3. CulParser: character positions ─────────────────────────────────────
    fprintf(stdout, "\n[ CulParser: character positions after write ]\n");
    {
        QString src = GENOTYPE + "/WHCER048.CUL";
        QStringList hdr;
        QVector<CulRow> rows = CulParser::parse(src, hdr);
        QStringList paramNames = CulParser::extractParamNames(hdr);

        QString dst = tmp.filePath("WHCER048_pos.CUL");
        CulParser::write(dst, rows, hdr, paramNames);

        QFile f(dst);
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        QTextStream in(&f);
        int badPos29 = 0, badEco = 0, rows_checked = 0;
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.isEmpty() || QString("!*@$").contains(line[0])) continue;
            if (line.length() < 36) continue;
            ++rows_checked;
            if (line[29] != ' ') ++badPos29;
            // ECO# at 30-35 must not be empty
            if (line.mid(30,6).trimmed().isEmpty()) ++badEco;
        }
        check(rows_checked > 0, "at least one data row written");
        check(badPos29 == 0,
              qPrintable(QString("pos 29 is always space (checked %1 rows)").arg(rows_checked)));
        check(badEco == 0, "ECO# never empty at pos 30-35");
    }

    // ── 4. CulParser: GLUE substr simulation ──────────────────────────────────
    fprintf(stdout, "\n[ CulParser: GLUE.r substr simulation ]\n");
    {
        QString src = GENOTYPE + "/WHCER048.CUL";
        QStringList hdr;
        QVector<CulRow> rows = CulParser::parse(src, hdr);
        QStringList paramNames = CulParser::extractParamNames(hdr);

        QString dst = tmp.filePath("WHCER048_glue.CUL");
        CulParser::write(dst, rows, hdr, paramNames);

        // GLUE.r: glue_view = substr(line,1,6) + substr(line,30,nchar) [R 1-based]
        // In 0-based C++: line[0..5] + line[29..]
        QFile f(dst);
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        QTextStream in(&f);
        int bad = 0;
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.isEmpty() || QString("!*@$").contains(line[0])) continue;
            if (line.length() < 36) continue;
            // pos 29 (0-based) must be space so GLUE's substr(line,30,...) starts at ECO#
            if (line[29] != ' ') ++bad;
        }
        check(bad == 0, "GLUE substr(line,30,...) always starts at ECO# (pos29=space)");
    }

    // ── 5. CulParser: all Genotype CUL files round-trip ──────────────────────
    fprintf(stdout, "\n[ CulParser: all Genotype CUL files round-trip ]\n");
    {
        QDir geno(GENOTYPE);
        QStringList culFiles = geno.entryList({"*.CUL"}, QDir::Files);
        int total = 0, failed = 0;
        for (const QString &fn : culFiles) {
            QString src = GENOTYPE + "/" + fn;
            QStringList hdr;
            QVector<CulRow> rows = CulParser::parse(src, hdr);
            if (rows.isEmpty()) continue; // skip empty/unrecognised
            ++total;
            QStringList paramNames = CulParser::extractParamNames(hdr);
            QString dst = tmp.filePath(fn + ".rt");
            if (!CulParser::write(dst, rows, hdr, paramNames)) { ++failed; continue; }
            QStringList hdr2;
            QVector<CulRow> rows2 = CulParser::parse(dst, hdr2);
            if (rows2.size() != rows.size()) ++failed;
        }
        check(total > 0, qPrintable(QString("found %1 CUL files").arg(total)));
        check(failed == 0,
              qPrintable(QString("all %1 CUL files round-trip without row-count change").arg(total)));
        if (failed > 0)
            fprintf(stderr, "  (%d files failed)\n", failed);
    }

    // ── 6. EcoParser: dollar-sign header preserved ────────────────────────────
    fprintf(stdout, "\n[ EcoParser: header lines ]\n");
    {
        // Find an ECO file that has a $ line
        QString src = GENOTYPE + "/WHCER048.ECO";
        QStringList hdr;
        QVector<EcoRow> rows = EcoParser::parse(src, hdr);
        check(!rows.isEmpty(), "WHCER048.ECO parsed non-empty");

        bool hasDollar = false;
        for (const QString &h : hdr) if (h.startsWith('$')) { hasDollar = true; break; }
        check(hasDollar, "$ version line preserved in ECO headerLines");
    }

    // ── 7. EcoParser: round-trip ──────────────────────────────────────────────
    fprintf(stdout, "\n[ EcoParser: round-trip write/parse ]\n");
    {
        QString src = GENOTYPE + "/WHCER048.ECO";
        QStringList hdr;
        QVector<EcoRow> rows = EcoParser::parse(src, hdr);
        QString dst = tmp.filePath("WHCER048_rt.ECO");
        bool wrote = EcoParser::write(dst, rows, hdr);
        check(wrote, "ECO write() returned true");

        QStringList hdr2;
        QVector<EcoRow> rows2 = EcoParser::parse(dst, hdr2);
        check(rows2.size() == rows.size(),
              qPrintable(QString("ECO row count preserved (%1)").arg(rows.size())));

        bool hasDollar2 = false;
        for (const QString &h : hdr2) if (h.startsWith('$')) { hasDollar2 = true; break; }
        check(hasDollar2, "ECO $ version line survives write+parse");
    }

    // ── 8. EcoParser: all Genotype ECO files round-trip ──────────────────────
    fprintf(stdout, "\n[ EcoParser: all Genotype ECO files round-trip ]\n");
    {
        QDir geno(GENOTYPE);
        QStringList ecoFiles = geno.entryList({"*.ECO"}, QDir::Files);
        int total = 0, failed = 0;
        for (const QString &fn : ecoFiles) {
            QString src = GENOTYPE + "/" + fn;
            QStringList hdr;
            QVector<EcoRow> rows = EcoParser::parse(src, hdr);
            if (rows.isEmpty()) continue;
            ++total;
            QString dst = tmp.filePath(fn + ".rt");
            if (!EcoParser::write(dst, rows, hdr)) { ++failed; continue; }
            QStringList hdr2;
            QVector<EcoRow> rows2 = EcoParser::parse(dst, hdr2);
            if (rows2.size() != rows.size()) ++failed;
        }
        check(total > 0, qPrintable(QString("found %1 ECO files").arg(total)));
        check(failed == 0,
              qPrintable(QString("all %1 ECO files round-trip without row-count change").arg(total)));
    }

    // ── 9. CulParser: expNo region format ─────────────────────────────────────
    fprintf(stdout, "\n[ CulParser: expNo region format ]\n");
    {
        // Construct rows with known expNo values and verify output
        struct Case { QString expNo; QString eco; QString expected29; };
        QList<Case> cases = {
            {"      .", "999991", " "},  // MINIMA dot
            {"   1,6 ", "USWH01", " "}, // experiment range
            {"      4", "CAWH01", " "}, // single digit
            {"       ", "DFAULT", " "}, // blank
        };
        int bad = 0;
        for (const Case &c : cases) {
            CulRow row;
            row.varNum = "IB0001";
            row.vrName = "TEST";
            row.expNo  = c.expNo;
            row.ecoNum = c.eco;
            QVector<ParamFormat> fmts(1);
            QString line = CulParser::formatRow(row, fmts, 0);
            if (line.length() < 30 || QString(line[29]) != c.expected29) {
                fprintf(stderr, "  expNo=%s -> pos29=%s (expected space)\n",
                        qPrintable(c.expNo), qPrintable(line.length()>29?QString(line[29]):"?"));
                ++bad;
            }
        }
        check(bad == 0, "all expNo variants produce space at pos 29");
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    fprintf(stdout, "\n================================\n");
    fprintf(stdout, "Results: %d passed, %d failed\n", s_pass, s_fail);
    fprintf(stdout, "================================\n\n");
    fflush(stdout);

    return s_fail > 0 ? 1 : 0;
}

// ── Headless GLUE run ─────────────────────────────────────────────────────────

int CommandLineHandler::runGlue(const CommandLineArgs &a)
{
    if (a.cropCode.isEmpty() || a.cultivarId.isEmpty()) {
        fprintf(stderr, "Usage: Gen2.exe --glue --crop WH --cultivar IB0488 --name NEWTON\n");
        return 1;
    }

    fprintf(stdout, "Starting headless GLUE: crop=%s cultivar=%s name=%s runs=%d mode=%s\n",
            qPrintable(a.cropCode), qPrintable(a.cultivarId),
            qPrintable(a.cultivarName), a.runs, qPrintable(a.mode));
    fflush(stdout);

    // Find RTerm
    QString rterm;
    QStringList versions = {"R-4.6.0","R-4.5.3","R-4.5.2","R-4.5.1","R-4.4.2","R-4.4.1","R-4.3.3"};
    for (const QString &ver : versions) {
        QString p = QString("C:/Program Files/R/%1/bin/x64/RTerm.exe").arg(ver);
        if (QFileInfo::exists(p)) { rterm = p; break; }
    }
    if (rterm.isEmpty()) {
        // scan
        QDir rBase("C:/Program Files/R");
        for (const QString &d : rBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString p = QString("C:/Program Files/R/%1/bin/x64/RTerm.exe").arg(d);
            if (QFileInfo::exists(p)) { rterm = p; break; }
        }
    }
    if (rterm.isEmpty()) rterm = "RTerm.exe";
    fprintf(stdout, "RTerm: %s\n", qPrintable(rterm)); fflush(stdout);

    // Run R
    QProcess proc;
    proc.setWorkingDirectory("C:/DSSAT48/Tools/GLUE");
    proc.setProgram(rterm);
    proc.setArguments({"--slave", "--file=GLUE.r"});
    proc.setProcessChannelMode(QProcess::MergedChannels);

    proc.start();
    if (!proc.waitForStarted(5000)) {
        fprintf(stderr, "Failed to start RTerm\n");
        return 1;
    }

    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(500);
        QByteArray out = proc.readAll();
        if (!out.isEmpty()) {
            fprintf(stdout, "%s", out.constData());
            fflush(stdout);
        }
        QCoreApplication::processEvents();
    }

    QByteArray remaining = proc.readAll();
    if (!remaining.isEmpty()) fprintf(stdout, "%s", remaining.constData());

    int code = proc.exitCode();
    fprintf(stdout, "\nGLUE finished with exit code %d\n", code);
    fflush(stdout);
    return code;
}

void CommandLineHandler::printUsage()
{
    fprintf(stdout,
        "Gen2 Command Line Usage:\n"
        "  Gen2.exe --test                          Run parser test suite\n"
        "  Gen2.exe --glue --crop WH                Run GLUE headlessly\n"
        "              --cultivar IB0488\n"
        "              --name NEWTON\n"
        "              [--runs 100]\n"
        "              [--mode phenology|growth|both]\n"
    );
}
