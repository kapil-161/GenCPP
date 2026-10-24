#include "CommandLineHandler.h"
#include "CulParser.h"
#include "EcoParser.h"
#include "DssatProParser.h"
#include "GlueRunner.h"
#include "Config.h"

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

#ifdef Q_OS_WIN
    const QString GENOTYPE = "C:/DSSAT48/Genotype";
#else
    const QString GENOTYPE = "/Applications/DSSAT48/Genotype";
#endif
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
            if (line.trimmed().isEmpty() || QString("!*@$").contains(line[0])) continue;
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
        auto commentLines = [](const QString &path) {
            QStringList out;
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                for (const QString &l : QString::fromLatin1(f.readAll()).split('\n'))
                    if (l.startsWith('!')) out << QString(l).remove('\r');
            return out;
        };
        int total = 0, failed = 0, lostComments = 0, badFormat = 0;
        for (const QString &fn : culFiles) {
            QString src = GENOTYPE + "/" + fn;
            QStringList hdr, ftr;
            QVector<CulRow> rows = CulParser::parse(src, hdr, &ftr);
            if (rows.isEmpty()) continue; // skip empty/unrecognised
            ++total;
            QStringList paramNames = CulParser::extractParamNames(hdr);
            QString dst = tmp.filePath(fn + ".rt");
            QFile::copy(src, dst);   // overwrite a copy, as the editor does (keeps EOL style)
            if (!CulParser::write(dst, rows, hdr, paramNames, ftr)) { ++failed; continue; }
            QStringList hdr2;
            QVector<CulRow> rows2 = CulParser::parse(dst, hdr2);
            if (rows2.size() != rows.size()) ++failed;

            // Every written data row must be strict fixed width: each parameter field is
            // exactly W chars, starts with a space, is right-aligned, and keeps its value.
            {
                const int numParams = paramNames.isEmpty() ? (rows.isEmpty() ? 0 : rows[0].params.size())
                                                           : paramNames.size();
                const QVector<ParamFormat> fmts = CulParser::inferFormats(rows, numParams);
                const int W = fmts.isEmpty() ? 6 : fmts[0].width + 1;
                QFile f(dst);
                f.open(QIODevice::ReadOnly);
                int r = 0;
                for (QString line : QString::fromLatin1(f.readAll()).split('\n')) {
                    line.remove('\r');
                    if (line.trimmed().isEmpty() || QString("*!@$").contains(line[0])) continue;
                    if (r >= rows.size()) break;
                    QString why;
                    if (line[6] != ' ' || line[29] != ' ') why = "pos 6/29 not space";
                    const int n = qMax(numParams, (int)rows[r].params.size());
                    if (why.isEmpty() && line.size() != 36 + n * W)
                        why = QString("length %1, expected %2").arg(line.size()).arg(36 + n * W);
                    for (int i = 0; why.isEmpty() && i < n; ++i) {
                        const QString fld = line.mid(36 + i * W, W);
                        if (fld[0] != ' ' || fld.endsWith(' ') || fld.contains('*'))
                            why = QString("field %1 \"%2\" not right-aligned").arg(i + 1).arg(fld);
                        const double orig = i < rows[r].params.size() && rows[r].params[i] ? *rows[r].params[i] : 0.0;
                        const QString t = fld.trimmed();
                        const int d = t.contains('.') ? t.size() - t.indexOf('.') - 1 : 0;
                        if (why.isEmpty() && std::abs(t.toDouble() - orig) > 0.5 * std::pow(10.0, -d) + 1e-9)
                            why = QString("field %1 value %2 != %3").arg(i + 1).arg(t).arg(orig);
                    }
                    if (!why.isEmpty()) {
                        ++badFormat;
                        fprintf(stderr, "  %s: %s\n    %s\n", qPrintable(fn), qPrintable(why),
                                qPrintable(line.left(110)));
                        break;
                    }
                    ++r;
                }
            }
            const QStringList before = commentLines(src), after = commentLines(dst);
            if (before != after) {
                ++lostComments;
                int i = 0;
                while (i < before.size() && i < after.size() && before[i] == after[i]) ++i;
                fprintf(stderr, "  %s: ! comment lines changed (%lld -> %lld) at #%d\n"
                                "    was: %s\n    now: %s\n",
                        qPrintable(fn), (long long)before.size(), (long long)after.size(), i + 1,
                        qPrintable(i < before.size() ? before[i] : "<none>"),
                        qPrintable(i < after.size() ? after[i] : "<none>"));
            }
        }
        check(total > 0, qPrintable(QString("found %1 CUL files").arg(total)));
        check(failed == 0,
              qPrintable(QString("all %1 CUL files round-trip without row-count change").arg(total)));
        if (failed > 0)
            fprintf(stderr, "  (%d files failed)\n", failed);
        check(lostComments == 0,
              qPrintable(QString("all %1 CUL files keep every ! comment line").arg(total)));
        check(badFormat == 0,
              qPrintable(QString("all %1 CUL files write strict fixed-width, right-aligned rows").arg(total)));
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
        QStringList hdr, ftr;
        EcoLayout lay;
        QVector<EcoRow> rows = EcoParser::parse(src, hdr, &ftr, &lay);
        QString dst = tmp.filePath("WHCER048_rt.ECO");
        bool wrote = EcoParser::write(dst, rows, hdr, lay, ftr);
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
        auto commentLines = [](const QString &path) {
            QStringList out;
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                for (const QString &l : QString::fromLatin1(f.readAll()).split('\n'))
                    if (l.startsWith('!')) out << QString(l).remove('\r');
            return out;
        };
        int total = 0, failed = 0, lostComments = 0, changedVals = 0, badFormat = 0, misalignedRows = 0;
        for (const QString &fn : ecoFiles) {
            QString src = GENOTYPE + "/" + fn;
            QStringList hdr, ftr;
            EcoLayout lay;
            QVector<EcoRow> rows = EcoParser::parse(src, hdr, &ftr, &lay);
            if (rows.isEmpty()) continue;
            ++total;
            QString dst = tmp.filePath(fn + ".rt");
            QFile::copy(src, dst);
            if (!EcoParser::write(dst, rows, hdr, lay, ftr)) { ++failed; continue; }
            QStringList hdr2;
            EcoLayout lay2;
            QVector<EcoRow> rows2 = EcoParser::parse(dst, hdr2, nullptr, &lay2);
            if (rows2.size() != rows.size()) { ++failed; continue; }

            if (commentLines(src) != commentLines(dst)) {
                ++lostComments;
                fprintf(stderr, "  %s: ! comment lines changed\n", qPrintable(fn));
            }
            // Same IDs, names and values (numbers compared by value); same column layout
            auto sameVals = [](const QStringList &a, const QStringList &b) {
                if (a.size() != b.size()) return false;
                for (int i = 0; i < a.size(); ++i) {
                    bool okA, okB;
                    const double x = a[i].toDouble(&okA), y = b[i].toDouble(&okB);
                    if (okA && okB ? std::abs(x - y) > 1e-9 : a[i] != b[i]) return false;
                }
                return true;
            };
            for (int r = 0; r < rows.size(); ++r) {
                if (rows[r].misaligned) {   // not valid fixed-width in the source; reported, not compared
                    ++misalignedRows;
                    fprintf(stdout, "  note: %s %s is not fixed-width in the source file\n",
                            qPrintable(fn), qPrintable(rows[r].ecoNum));
                    continue;
                }
                if (rows[r].ecoNum != rows2[r].ecoNum || rows[r].ecoName != rows2[r].ecoName
                    || !sameVals(rows[r].vals, rows2[r].vals)) {
                    ++changedVals;
                    fprintf(stderr, "  %s: row %s changed\n    was: %s\n    now: %s\n", qPrintable(fn),
                            qPrintable(rows[r].ecoNum), qPrintable(rows[r].vals.join(' ')),
                            qPrintable(rows2[r].vals.join(' ')));
                    break;
                }
            }
            if (lay.ends != lay2.ends) {
                ++badFormat;
                fprintf(stderr, "  %s: column layout changed after save\n", qPrintable(fn));
            }
            // Every written data row: each value right-aligned to its column edge
            for (const EcoRow &row : rows) {
                const QString line = EcoParser::formatRow(row, lay);
                QString why;
                const int n = qMin(int(row.vals.size()), int(lay.ends.size()));
                for (int c = 0; why.isEmpty() && c < n; ++c) {
                    const QString fld = line.mid(lay.ends[c] - lay.width(c), lay.width(c));
                    if (fld.size() != lay.width(c) || fld[0] != ' ' || fld.endsWith(' '))
                        why = QString("column %1 \"%2\" not right-aligned").arg(lay.names[c], fld);
                }
                if (why.isEmpty() && n > 0 && line.size() != lay.ends[n - 1])
                    why = QString("length %1, expected %2").arg(line.size()).arg(lay.ends[n - 1]);
                if (!why.isEmpty()) {
                    ++badFormat;
                    fprintf(stderr, "  %s: %s\n    %s\n", qPrintable(fn), qPrintable(why),
                            qPrintable(line.left(110)));
                    break;
                }
            }
        }
        check(total > 0, qPrintable(QString("found %1 ECO files").arg(total)));
        check(failed == 0,
              qPrintable(QString("all %1 ECO files round-trip without row-count change").arg(total)));
        check(lostComments == 0,
              qPrintable(QString("all %1 ECO files keep every ! comment line").arg(total)));
        check(changedVals == 0,
              qPrintable(QString("all %1 ECO files keep every ID, name and value").arg(total)));
        check(badFormat == 0,
              qPrintable(QString("all %1 ECO files write strict fixed-width, right-aligned rows").arg(total)));
    }

    // ── 8a. CulParser: formatParam is always exactly width+1, right-aligned ──
    fprintf(stdout, "\n[ CulParser: formatParam fixed width ]\n");
    {
        ParamFormat f; f.width = 5; f.decimals = 3;
        check(CulParser::formatParam(-0.006, f) == " -.006", "-0.006 (3 dec) -> \" -.006\"");
        check(CulParser::formatParam(0.200, f)  == " 0.200", "0.2 (3 dec) -> \" 0.200\"");
        f.decimals = 1;
        check(CulParser::formatParam(12345.678, f) == " 12346", "12345.678 (1 dec) -> \" 12346\"");
        check(CulParser::formatParam(2.5, f) == "   2.5", "2.5 (1 dec) -> \"   2.5\"");
        f.trailingDot = true; f.decimals = 0;
        check(CulParser::formatParam(240, f) == "  240.", "240 (trailing dot) -> \"  240.\"");
        f.trailingDot = false; f.decimals = 2; f.width = 14;
        check(CulParser::formatParam(5.7, f) == QString(" ") + QString("5.70").rightJustified(14),
              "wide (15-char) field stays 15 chars");
    }

    // ── 8b. CulParser: nextFreeId keeps 6-char IDs ───────────────────────────
    fprintf(stdout, "\n[ CulParser: nextFreeId ]\n");
    {
        QStringList ids = {"999991", "999992", "IB0001", "IB0488", "IB1500", "AW0071", "C00001"};
        check(CulParser::nextFreeId("IB0488", ids) == "IB1501", "IB0488 -> IB1501");
        check(CulParser::nextFreeId("C00001", ids) == "C00002", "C00001 -> C00002");
        check(CulParser::nextFreeId("DFAULT", ids).size() == 6, "DFAULT -> 6-char id");
        bool allSix = true;
        for (const QString &s : ids) {
            QString id = CulParser::nextFreeId(s, ids);
            if (id.size() != 6 || ids.contains(id)) allSix = false;
        }
        check(allSix, "every generated id is 6 chars and unused");
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
        fprintf(stderr, "Usage: GeneticsEditor.exe --glue --crop WH --cultivar IB0488 "
                        "--name NEWTON [--runs 100] [--mode phenology|growth|both]\n");
        return 1;
    }

    fprintf(stdout, "GLUE: crop=%s  cultivar=%s  name=%s  runs=%d  mode=%s\n",
            qPrintable(a.cropCode), qPrintable(a.cultivarId),
            qPrintable(a.cultivarName), a.runs, qPrintable(a.mode));
    fflush(stdout);

    // GLUE_DIR / GLUE_WORK: same discovery the GUI does at startup
    GlueRunner::resolvePaths(Config::DSSATPRO_FILE);
    fprintf(stdout, "GLUE dir: %s  Work dir: %s\n",
            qPrintable(GlueRunner::GLUE_DIR), qPrintable(GlueRunner::GLUE_WORK));

    // ── 1. Resolve CropInfo ───────────────────────────────────────────────────
    QMap<QString, CropInfo> crops = DssatProParser::discoverCrops(Config::DSSATPRO_FILE);
    CropInfo cropInfo;
    for (const CropInfo &c : crops) {
        if (c.cropCode.compare(a.cropCode, Qt::CaseInsensitive) != 0) continue;
        // Prefer the DSSATPRO-designated primary model; fall back to any match
        if (cropInfo.cropCode.isEmpty() || c.isPrimary)
            cropInfo = c;
    }
    if (cropInfo.cropCode.isEmpty()) {
        fprintf(stderr, "ERROR: crop '%s' not found in DSSATPRO.v48\n",
                qPrintable(a.cropCode));
        return 1;
    }
    fprintf(stdout, "Crop: %s  Module: %s  ExpDir: %s\n",
            qPrintable(cropInfo.cropCode), qPrintable(cropInfo.module),
            qPrintable(cropInfo.expDir));
    fflush(stdout);

    // ── 2. Scan experiments ───────────────────────────────────────────────────
    fprintf(stdout, "Scanning experiments in: %s\n", qPrintable(cropInfo.expDir));
    fflush(stdout);

    ScanResult scan = GlueRunner::scanExperiments(cropInfo, a.cultivarId, false);
    if (!scan.errorMsg.isEmpty()) {
        fprintf(stderr, "ERROR: %s\n", qPrintable(scan.errorMsg));
        return 1;
    }
    if (scan.filesScanned == 0) {
        fprintf(stderr, "ERROR: No .%sX experiment files found in: %s\n",
                qPrintable(a.cropCode), qPrintable(cropInfo.expDir));
        return 1;
    }
    if (scan.treatments.isEmpty()) {
        fprintf(stderr, "ERROR: Cultivar %s not found in any of %d experiment file(s)\n",
                qPrintable(a.cultivarId), scan.filesScanned);
        return 1;
    }

    // Count total treatments
    int totalTreatments = 0;
    for (const auto &list : scan.treatments) totalTreatments += list.size();
    fprintf(stdout, "Found %d treatment(s) across %d file(s)\n",
            totalTreatments, static_cast<int>(scan.treatments.size()));
    for (auto it = scan.treatments.begin(); it != scan.treatments.end(); ++it) {
        for (const TreatmentEntry &e : it.value())
            fprintf(stdout, "  %s  trt %d\n",
                    qPrintable(QDir::toNativeSeparators(it.key())), e.number);
    }
    fflush(stdout);

    // ── 3. Write DSSBatch file ────────────────────────────────────────────────
    QString batchPath = GlueRunner::writeBatchFile(
        cropInfo, a.cultivarId, a.cultivarName, scan.treatments);
    if (batchPath.isEmpty()) {
        fprintf(stderr, "ERROR: Cannot write batch file to %s\n",
                qPrintable(GlueRunner::GLUE_WORK));
        return 1;
    }
    fprintf(stdout, "Batch file: %s\n", qPrintable(QDir::toNativeSeparators(batchPath)));
    fflush(stdout);

    // ── 4. Update SimulationControl.csv ──────────────────────────────────────
    // Map --mode string to GLUEFlag integer: 1=both, 2=phenology, 3=growth
    int glueFlag = 1;
    if (a.mode == "phenology") glueFlag = 2;
    else if (a.mode == "growth") glueFlag = 3;

    if (!GlueRunner::updateSimControl(cropInfo, a.cultivarId, a.runs, glueFlag, "N")) {
        fprintf(stderr, "ERROR: Cannot update %s/SimulationControl.csv\n",
                qPrintable(GlueRunner::GLUE_DIR));
        return 1;
    }
    fprintf(stdout, "SimulationControl.csv updated (runs=%d, glueFlag=%d)\n",
            a.runs, glueFlag);
    fflush(stdout);

    // ── 5. Launch RTerm ───────────────────────────────────────────────────────
    QString rterm = GlueRunner::findRTerm();
    fprintf(stdout, "RTerm: %s\n", qPrintable(rterm));
    fprintf(stdout, "--- GLUE output ---\n");
    fflush(stdout);

    QProcess proc;
    proc.setWorkingDirectory(GlueRunner::GLUE_DIR);
    proc.setProgram(rterm);
#ifdef Q_OS_WIN
    // RTerm.exe needs -f to run a script file (same as GlueQueueManager)
    proc.setArguments({"--slave", "-f", GlueRunner::GLUE_DIR + "/GLUE.r"});
#else
    // Rscript: --file= makes R 4.4+ on macOS exit with "file name is missing"
    proc.setArguments({"--slave", GlueRunner::GLUE_DIR + "/GLUE.r"});
#endif
    proc.setProcessChannelMode(QProcess::MergedChannels);

    proc.start();
    if (!proc.waitForStarted(5000)) {
        fprintf(stderr, "ERROR: Failed to start RTerm\n");
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
    if (!remaining.isEmpty()) {
        fprintf(stdout, "%s", remaining.constData());
        fflush(stdout);
    }

    int code = proc.exitCode();
    fprintf(stdout, "\n--- GLUE finished (exit code %d) ---\n", code);
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
