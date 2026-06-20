#include "GlueWizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDirIterator>
#include <QLabel>
#include <QSplitter>
#include <QProcess>
#include <QDateTime>
#include <QRegularExpression>
#include <QFont>

static const QString GLUE_DIR       = "C:/DSSAT48/Tools/GLUE";
static const QString GLUE_WORK      = "C:/DSSAT48/GLWork";
static const QString BACKUP_DEFAULT = "C:/DSSAT48/GLWork/BackUp";

static QString findRTerm()
{
    // Try versions from newest to oldest
    QStringList versions = {"R-4.6.0","R-4.5.3","R-4.5.2","R-4.5.1","R-4.4.2","R-4.4.1","R-4.3.3"};
    QStringList bases = {"C:/Program Files/R", "C:/PROGRA~1/R"};
    for (const QString &base : bases) {
        for (const QString &ver : versions) {
            QString path = base + "/" + ver + "/bin/x64/RTerm.exe";
            if (QFileInfo::exists(path)) return path;
        }
        // Also try any installed version via directory scan
        QDir rBase(base);
        for (const QString &d : rBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString path = base + "/" + d + "/bin/x64/RTerm.exe";
            if (QFileInfo::exists(path)) return path;
        }
    }
    return "RTerm.exe"; // fallback to PATH
}

GlueWizard::GlueWizard(const CropInfo &cropInfo,
                       const QString &cultivarId,
                       const QString &cultivarName,
                       QWidget *parent)
    : QDialog(parent)
    , m_cropInfo(cropInfo)
    , m_cultivarId(cultivarId)
    , m_cultivarName(cultivarName)
{
    setWindowTitle(QString("Run GLUE — %1 / %2 %3")
                   .arg(cropInfo.cropCode, cultivarId, cultivarName));
    setMinimumSize(620, 480);

    m_stack = new QStackedWidget(this);
    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(m_stack);

    setupTreatmentPage();
    setupBackupPage();
    setupRunPage();

    m_stack->setCurrentIndex(0);
    scanExperiments();
}

// ── Page 1: treatment tree ────────────────────────────────────────────────────
void GlueWizard::setupTreatmentPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *vbox = new QVBoxLayout(page);

    QLabel *title = new QLabel(
        QString("<b>Select Treatments to Include</b><br>"
                "<small>Crop: %1 &nbsp; Cultivar: %2 %3</small>")
        .arg(m_cropInfo.cropCode, m_cultivarId, m_cultivarName));
    vbox->addWidget(title);

    QHBoxLayout *content = new QHBoxLayout;

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    content->addWidget(m_tree, 1);

    QVBoxLayout *btnCol = new QVBoxLayout;
    btnCol->setAlignment(Qt::AlignTop);
    m_selectAllBtn   = new QPushButton("Select All");
    m_unselectAllBtn = new QPushButton("Unselect All");
    m_goBtn          = new QPushButton("Go !");
    m_goBtn->setDefault(true);
    btnCol->addWidget(m_selectAllBtn);
    btnCol->addWidget(m_unselectAllBtn);
    btnCol->addStretch();
    btnCol->addWidget(m_goBtn);
    content->addLayout(btnCol);

    vbox->addLayout(content);

    connect(m_selectAllBtn,   &QPushButton::clicked, this, &GlueWizard::onSelectAll);
    connect(m_unselectAllBtn, &QPushButton::clicked, this, &GlueWizard::onUnselectAll);
    connect(m_goBtn,          &QPushButton::clicked, this, &GlueWizard::onGoFromTreatments);

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        QTreeWidgetItem *parent = item->parent();
        if (!parent) return; // it's a top-level (experiment) item, ignore
        // Block signals to avoid recursion
        m_tree->blockSignals(true);
        bool anyChecked = false;
        for (int i = 0; i < parent->childCount(); ++i)
            if (parent->child(i)->checkState(0) == Qt::Checked) { anyChecked = true; break; }
        parent->setCheckState(0, anyChecked ? Qt::Checked : Qt::Unchecked);
        m_tree->blockSignals(false);
    });

    m_stack->addWidget(page);
}

// ── Page 2: backup ───────────────────────────────────────────────────────────
void GlueWizard::setupBackupPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *vbox = new QVBoxLayout(page);
    vbox->addStretch();

    QLabel *lbl = new QLabel("<b>Backup existing GLUE work files before running?</b>");
    lbl->setAlignment(Qt::AlignCenter);
    vbox->addWidget(lbl);

    QHBoxLayout *dirRow = new QHBoxLayout;
    dirRow->addWidget(new QLabel("Backup folder:"));
    m_backupDirEdit = new QLineEdit(BACKUP_DEFAULT);
    dirRow->addWidget(m_backupDirEdit, 1);
    m_backupBrowseBtn = new QPushButton("Browse…");
    dirRow->addWidget(m_backupBrowseBtn);
    vbox->addLayout(dirRow);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->setAlignment(Qt::AlignCenter);
    m_backupYesBtn = new QPushButton("Yes — Backup");
    m_backupNoBtn  = new QPushButton("No — Skip");
    m_backupYesBtn->setFixedWidth(130);
    m_backupNoBtn->setFixedWidth(130);
    btnRow->addWidget(m_backupYesBtn);
    btnRow->addWidget(m_backupNoBtn);
    vbox->addLayout(btnRow);

    vbox->addStretch();

    connect(m_backupBrowseBtn, &QPushButton::clicked, this, &GlueWizard::onBrowseBackup);
    connect(m_backupYesBtn, &QPushButton::clicked, this, [this]() {
        QString dest = m_backupDirEdit->text().trimmed();
        QDir().mkpath(dest);
        QDirIterator it(GLUE_WORK, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString src = it.next();
            QString rel = QDir(GLUE_WORK).relativeFilePath(src);
            QString dst = dest + "/" + rel;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            QFile::copy(src, dst);
        }
        m_stack->setCurrentIndex(2);
    });
    connect(m_backupNoBtn, &QPushButton::clicked, this, [this]() {
        m_stack->setCurrentIndex(2);
    });

    m_stack->addWidget(page);
}

// ── Page 3: run ──────────────────────────────────────────────────────────────
void GlueWizard::setupRunPage()
{
    QWidget *page = new QWidget;
    QHBoxLayout *hbox = new QHBoxLayout(page);

    // Left: GLUE Parameters
    QGroupBox *paramBox = new QGroupBox("GLUE Parameters");
    QGridLayout *grid = new QGridLayout(paramBox);

    grid->addWidget(new QLabel("Runs:"), 0, 0);
    m_runsSpin = new QSpinBox;
    m_runsSpin->setRange(100, 1000000);
    m_runsSpin->setValue(20000);
    m_runsSpin->setSingleStep(1000);
    grid->addWidget(m_runsSpin, 0, 1);

    grid->addWidget(new QLabel("Mode:"), 1, 0);
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("Both (Phenology + Growth)", 1);
    m_modeCombo->addItem("Phenology Only",            2);
    m_modeCombo->addItem("Growth Parameters",         3);
    m_modeCombo->setCurrentIndex(0);
    grid->addWidget(m_modeCombo, 1, 1);

    grid->addWidget(new QLabel("Include ECO file:"), 2, 0);
    m_ecoCheck = new QCheckBox;
    grid->addWidget(m_ecoCheck, 2, 1);

    hbox->addWidget(paramBox);

    // Center: action buttons + log
    QVBoxLayout *centerCol = new QVBoxLayout;
    m_runGlueBtn  = new QPushButton("Run GLUE");
    m_stopGlueBtn = new QPushButton("Stop GLUE");
    m_startOverBtn = new QPushButton("Start Over");
    m_stopGlueBtn->setEnabled(false);

    m_runGlueBtn->setStyleSheet("font-weight:bold; background:#2196F3; color:white;");
    centerCol->addWidget(m_runGlueBtn);
    centerCol->addWidget(m_stopGlueBtn);
    centerCol->addWidget(m_startOverBtn);

    m_logEdit = new QTextEdit;
    m_logEdit->setReadOnly(true);
    m_logEdit->setFont(QFont("Courier New", 8));
    centerCol->addWidget(m_logEdit, 1);
    hbox->addLayout(centerCol, 1);

    // Right: outputs
    QGroupBox *outBox = new QGroupBox("GLUE Outputs");
    QVBoxLayout *outCol = new QVBoxLayout(outBox);
    m_outCoeffBtn = new QPushButton("Cultivar Coefficients");
    m_outDevBtn   = new QPushButton("Development");
    m_outYieldBtn = new QPushButton("Growth and Yield");
    for (auto *b : {m_outCoeffBtn, m_outDevBtn, m_outYieldBtn}) {
        b->setEnabled(false);
        outCol->addWidget(b);
    }
    outCol->addStretch();
    hbox->addWidget(outBox);

    connect(m_runGlueBtn,  &QPushButton::clicked, this, &GlueWizard::onRunGlue);
    connect(m_stopGlueBtn, &QPushButton::clicked, this, &GlueWizard::onStopGlue);
    connect(m_startOverBtn, &QPushButton::clicked, this, &GlueWizard::onStartOver);

    // Output buttons open the result files
    connect(m_outCoeffBtn, &QPushButton::clicked, this, [this]() {
        QProcess::startDetached("notepad", {GLUE_WORK + "/OptimalParameterSet.txt"});
    });
    connect(m_outDevBtn, &QPushButton::clicked, this, [this]() {
        QProcess::startDetached("notepad", {GLUE_WORK + "/Evaluate_output.txt"});
    });
    connect(m_outYieldBtn, &QPushButton::clicked, this, [this]() {
        QProcess::startDetached("notepad", {GLUE_WORK + "/EvaluateFrame_2.txt"});
    });

    m_stack->addWidget(page);
}

// ── Scan experiments ──────────────────────────────────────────────────────────
// For each experiment file (*X):
//   1. Parse *CULTIVARS section: find the 6-digit cultivar ID -> get its @C number
//   2. Parse *TREATMENTS section: find treatments where CU column == that @C number
//   3. List only those matching treatments
void GlueWizard::scanExperiments()
{
    m_tree->clear();

    QString xExt = m_cropInfo.cropCode + "X";
    QDirIterator it(m_cropInfo.expDir,
                    QStringList() << "*." + xExt << "*." + xExt.toLower(),
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QStringList allLines;
        QTextStream in(&f);
        while (!in.atEnd()) allLines << in.readLine();
        f.close();

        // ── Step 1: find cultivar number from *CULTIVARS section ──────────────
        // Header: @C CR INGENO CNAME
        // Data:    1 LU LU0001 Rex
        // We look for m_cultivarId (6-digit) in INGENO column (col index 2)
        int cultivarNum = -1;
        bool inCulSection = false;
        int cuCol = -1; // column index of INGENO in the header
        int crCol = -1; // column index of CR in the header

        for (const QString &line : allLines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('!')) continue;

            if (trimmed.startsWith("*CULTIVAR")) {
                inCulSection = true;
                cuCol = -1;
                continue;
            }
            if (inCulSection) {
                if (trimmed.startsWith('*')) { inCulSection = false; continue; }
                if (trimmed.startsWith("@C")) {
                    QStringList hdrs = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    cuCol = hdrs.indexOf("INGENO");
                    crCol = hdrs.indexOf("CR");
                    continue;
                }
                if (trimmed.startsWith('@')) continue;
                if (cuCol < 0) continue;

                QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() <= cuCol) continue;
                // Verify CR matches crop code to avoid cross-crop false matches
                if (crCol >= 0 && crCol < parts.size() &&
                    parts[crCol].compare(m_cropInfo.cropCode, Qt::CaseInsensitive) != 0)
                    continue;
                if (parts[cuCol].compare(m_cultivarId, Qt::CaseInsensitive) == 0) {
                    cultivarNum = parts[0].toInt();
                    break;
                }
            }
        }

        if (cultivarNum < 0) continue; // cultivar not in this file

        // ── Step 2: find CU column index from *TREATMENTS header ──────────────
        // Header: @N R O C TNAME.................... CU FL SA ...
        // Data:    1 1 0 0 Rex_24C                    1  1  0 ...
        QStringList matchingTreatments;
        bool inTrtSection = false;
        int cuTrtCol = -1; // token index of CU in treatment data line
        int tnameStart = -1; // character position of TNAME in the header line

        for (const QString &line : allLines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('!')) continue;

            if (trimmed.startsWith("*TREATMENT")) {
                inTrtSection = true;
                cuTrtCol = -1;
                continue;
            }
            if (inTrtSection) {
                if (trimmed.startsWith('*')) { inTrtSection = false; continue; }

                if (trimmed.startsWith("@N")) {
                    // Find character position of TNAME and token index of CU
                    tnameStart = line.indexOf("TNAME");
                    QStringList hdrs = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    cuTrtCol = hdrs.indexOf("CU");
                    continue;
                }
                if (trimmed.startsWith('@')) continue;
                if (cuTrtCol < 0 || tnameStart < 0) continue;

                QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() <= cuTrtCol) continue;

                bool ok;
                int trtNum = parts[0].toInt(&ok);
                if (!ok) continue;

                int cuVal = parts[cuTrtCol].toInt();
                if (cuVal != cultivarNum) continue;

                // Extract TNAME by character position (25 chars wide)
                QString tname;
                if (line.length() > tnameStart)
                    tname = line.mid(tnameStart, 25).trimmed();
                if (tname.isEmpty())
                    tname = parts.value(4); // fallback

                matchingTreatments << QString("[%1] %2").arg(trtNum).arg(tname);
            }
        }

        if (matchingTreatments.isEmpty()) continue;

        QTreeWidgetItem *parent = new QTreeWidgetItem(m_tree);
        parent->setText(0, filePath);
        parent->setCheckState(0, Qt::Unchecked);
        parent->setExpanded(true);

        for (const QString &tr : matchingTreatments) {
            QTreeWidgetItem *child = new QTreeWidgetItem(parent);
            child->setText(0, tr);
            child->setCheckState(0, Qt::Unchecked);
        }
    }

    if (m_tree->topLevelItemCount() == 0) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QString("(No experiments found for cultivar %1 in %2)")
                         .arg(m_cultivarId, m_cropInfo.expDir));
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        m_goBtn->setEnabled(false);
    }
}

// ── Slot: Select All ──────────────────────────────────────────────────────────
void GlueWizard::onSelectAll()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *parent = m_tree->topLevelItem(i);
        parent->setCheckState(0, Qt::Checked);
        for (int j = 0; j < parent->childCount(); ++j)
            parent->child(j)->setCheckState(0, Qt::Checked);
    }
}

void GlueWizard::onUnselectAll()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *parent = m_tree->topLevelItem(i);
        parent->setCheckState(0, Qt::Unchecked);
        for (int j = 0; j < parent->childCount(); ++j)
            parent->child(j)->setCheckState(0, Qt::Unchecked);
    }
}

// ── Slot: Go from treatments → backup ────────────────────────────────────────
void GlueWizard::onGoFromTreatments()
{
    // Collect selected treatments: filePath -> list of treatment numbers
    QMap<QString, QList<int>> selected;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *expItem = m_tree->topLevelItem(i);
        QString filePath = expItem->text(0);
        for (int j = 0; j < expItem->childCount(); ++j) {
            QTreeWidgetItem *trtItem = expItem->child(j);
            if (trtItem->checkState(0) != Qt::Checked) continue;
            // Text format: "[1] TreatmentName" — extract number
            QString txt = trtItem->text(0);
            int from = txt.indexOf('[') + 1;
            int to   = txt.indexOf(']');
            if (from > 0 && to > from)
                selected[filePath] << txt.mid(from, to - from).toInt();
        }
    }

    if (selected.isEmpty()) {
        QMessageBox::warning(this, "No selection",
                             "Please select at least one treatment.");
        return;
    }

    // Generate batch file: $BATCH(CULTIVAR):cropCode + cultivarId + " " + cultivarName
    // GLUE.r reads: chars 18-19 = cropCode, chars 20-25 = cultivarId
    // Format: "$BATCH(CULTIVAR):" (17 chars) + cropCode(2) + cultivarId(6) + " " + cultivarName
    QString batchFileName = QString("%1.%2C").arg(m_cultivarId, m_cropInfo.cropCode);
    QString batchPath = GLUE_WORK + "/" + batchFileName;

    QDir().mkpath(GLUE_WORK);
    QFile batchFile(batchPath);
    if (!batchFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error",
                              "Cannot write batch file:\n" + batchPath);
        return;
    }

    QTextStream out(&batchFile);
    // Header line — fixed format GLUE.r expects
    // chars 1-17: "$BATCH(CULTIVAR):", 18-19: cropCode, 20-25: cultivarId, 26+: " name"
    // Limit cultivar name to 16 chars (VRNAME field width) to avoid bleed from adjacent fields
    QString culName = m_cultivarName.trimmed().left(16).trimmed();
    out << QString("$BATCH(CULTIVAR):%1%2 %3\n")
           .arg(m_cropInfo.cropCode, m_cultivarId, culName);
    out << " \n";
    // Column header: "@FILEX" + spaces to col 94 + "TRTNO     RP     SQ     OP     CO"
    // Total header line length must be 127 chars (matching DSSAT expectation)
    out << QString("@FILEX%1TRTNO     RP     SQ     OP     CO\n")
           .arg(QString(88, ' '));

    // One data line per selected treatment
    // File path padded to 93 chars, TRTNO right-aligned in 6, then fixed columns
    for (auto it = selected.begin(); it != selected.end(); ++it) {
        QString filePath = QDir::toNativeSeparators(it.key());
        for (int trtNo : it.value()) {
            QString padded = filePath.leftJustified(93, ' ');
            out << QString("%1%2      0      0      0      0\n")
                   .arg(padded).arg(trtNo, 6);
        }
    }
    batchFile.close();

    m_selectedFiles = selected.keys();
    m_stack->setCurrentIndex(1);
}

QStringList GlueWizard::selectedTreatmentFiles()
{
    return m_selectedFiles;
}

// ── Slot: Back ────────────────────────────────────────────────────────────────
void GlueWizard::onBackToTreatments()
{
    m_stack->setCurrentIndex(0);
}

// ── Slot: Browse backup folder ────────────────────────────────────────────────
void GlueWizard::onBrowseBackup()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Backup Folder", m_backupDirEdit->text());
    if (!dir.isEmpty())
        m_backupDirEdit->setText(dir);
}

// ── Slot: Run GLUE ────────────────────────────────────────────────────────────
void GlueWizard::onRunGlue()
{
    // Write SimulationControl.csv with current settings
    int glueFlag = m_modeCombo->currentData().toInt();
    QString ecoCalib = m_ecoCheck->isChecked() ? "Y" : "N";
    int runs = m_runsSpin->value();

    QString simCtrlPath = GLUE_DIR + "/SimulationControl.csv";
    QFile sc(simCtrlPath);
    if (!sc.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Cannot open SimulationControl.csv");
        return;
    }
    QStringList lines;
    QTextStream in(&sc);
    while (!in.atEnd()) lines << in.readLine();
    sc.close();

    // Update relevant fields
    for (QString &line : lines) {
        if (line.startsWith("NumberOfModelRun,"))
            line = QString("NumberOfModelRun,%1").arg(runs);
        else if (line.startsWith("GLUEFlag,"))
            line = QString("GLUEFlag,%1").arg(glueFlag);
        else if (line.startsWith("EcotypeCalibration,"))
            line = QString("EcotypeCalibration,%1").arg(ecoCalib);
        else if (line.startsWith("CultivarBatchFile,"))
            line = QString("CultivarBatchFile,%1.%2C").arg(m_cultivarId, m_cropInfo.cropCode);
        else if (line.startsWith("ModelID,"))
            line = QString("ModelID,%1").arg(m_cropInfo.module);
    }

    QFile scOut(simCtrlPath);
    if (!scOut.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Cannot write SimulationControl.csv");
        return;
    }
    QTextStream out(&scOut);
    for (const QString &l : lines) out << l << "\n";
    scOut.close();

    m_logEdit->clear();
    m_logEdit->append("Starting GLUE calibration...");
    m_logEdit->append(QString("Cultivar: %1 %2").arg(m_cultivarId, m_cultivarName));
    m_logEdit->append(QString("Runs: %1  Mode: %2  ECO: %3")
                      .arg(runs).arg(m_modeCombo->currentText()).arg(ecoCalib));
    m_logEdit->append("---");

    m_glueProcess = new QProcess(this);
    m_glueProcess->setWorkingDirectory(GLUE_DIR);
    connect(m_glueProcess, &QProcess::readyReadStandardOutput, this, &GlueWizard::onGlueOutput);
    connect(m_glueProcess, &QProcess::readyReadStandardError,  this, &GlueWizard::onGlueOutput);
    connect(m_glueProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus){ onGlueFinished(code); });

    QString rterm = findRTerm();
    m_glueProcess->start(rterm, {"--slave", "--file=" + GLUE_DIR + "/GLUE.r"});

    m_runGlueBtn->setEnabled(false);
    m_stopGlueBtn->setEnabled(true);
}

void GlueWizard::onStopGlue()
{
    if (m_glueProcess && m_glueProcess->state() != QProcess::NotRunning) {
        m_glueProcess->kill();
        m_logEdit->append("\n[Stopped by user]");
    }
}

void GlueWizard::onStartOver()
{
    onStopGlue();
    m_logEdit->clear();
    m_runGlueBtn->setEnabled(true);
    m_stopGlueBtn->setEnabled(false);
    for (auto *b : {m_outCoeffBtn, m_outDevBtn, m_outYieldBtn})
        b->setEnabled(false);
    m_stack->setCurrentIndex(0);
    scanExperiments();
}

void GlueWizard::onGlueOutput()
{
    if (!m_glueProcess) return;
    QByteArray out = m_glueProcess->readAllStandardOutput();
    QByteArray err = m_glueProcess->readAllStandardError();
    if (!out.isEmpty()) m_logEdit->append(QString::fromLocal8Bit(out).trimmed());
    if (!err.isEmpty()) m_logEdit->append(QString::fromLocal8Bit(err).trimmed());
}

void GlueWizard::onGlueFinished(int exitCode)
{
    m_runGlueBtn->setEnabled(true);
    m_stopGlueBtn->setEnabled(false);

    QString log = m_logEdit->toPlainText();
    bool hasError = log.contains("error occurred", Qt::CaseInsensitive) ||
                    log.contains("cannot open", Qt::CaseInsensitive) ||
                    log.contains("Error in ", Qt::CaseInsensitive) ||
                    exitCode != 0;

    if (!hasError) {
        m_logEdit->append("\n✓ GLUE calibration finished successfully.");
        for (auto *b : {m_outCoeffBtn, m_outDevBtn, m_outYieldBtn})
            b->setEnabled(true);
    } else {
        m_logEdit->append(QString("\n✗ GLUE encountered errors (exit code %1). Check log above.").arg(exitCode));
    }
}
