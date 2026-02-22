#include "MainWindow.h"
#include "CulParser.h"
#include "EcoParser.h"
#include "BackupManager.h"
#include "SpeEditor.h"
#include "SpeSyntaxHighlighter.h"
#include "Config.h"
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QInputDialog>
#include <QClipboard>
#include <QShortcut>
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>
#include <QTimer>
#include <QDebug>
#include <memory>

// ─── Proxy: pins MINIMA/MAXIMA rows to the top during any sort ───────────────
class CulSortProxy : public QSortFilterProxyModel {
public:
    explicit CulSortProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        // VAR# is always column 0 in the source model
        const QString lv = sourceModel()->data(sourceModel()->index(left.row(),  0)).toString();
        const QString rv = sourceModel()->data(sourceModel()->index(right.row(), 0)).toString();
        const bool lPin = (lv == "999991" || lv == "999992");
        const bool rPin = (rv == "999991" || rv == "999992");
        if (lPin != rPin) {
            // Keep pinned rows above all regular rows regardless of sort direction.
            // For ascending:  pinned < normal  → return lPin
            // For descending: Qt reverses lessThan, so returning !lPin keeps them on top too
            return (sortOrder() == Qt::AscendingOrder) ? lPin : !lPin;
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

class EcoSortProxy : public QSortFilterProxyModel {
public:
    explicit EcoSortProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        // ECO# is always column 0 in the source model
        const QString lv = sourceModel()->data(sourceModel()->index(left.row(),  0)).toString();
        const QString rv = sourceModel()->data(sourceModel()->index(right.row(), 0)).toString();
        const bool lPin = (lv == "999991" || lv == "999992");
        const bool rPin = (rv == "999991" || rv == "999992");
        if (lPin != rPin) {
            // Keep pinned rows (MINIMA/MAXIMA) above all regular rows regardless of sort direction
            return (sortOrder() == Qt::AscendingOrder) ? lPin : !lPin;
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

// ─── constructor ────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_culModel(new CulTableModel(this))
    , m_ecoModel(new EcoTableModel(this))
{
    setWindowTitle(QString("%1 v%2 — DSSAT Genetics Editor")
                   .arg(Config::APP_NAME, Config::APP_VERSION));
    setMinimumSize(Config::WIN_MIN_W, Config::WIN_MIN_H);
    resize(Config::WIN_W, Config::WIN_H);

    // Global stylesheet matching GB2CPP style
    setStyleSheet(
        "* { color: #000000; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QMainWindow, QWidget { background-color: #F0F5F9; }"
        "QTabWidget::pane { border: 1px solid #E4E8ED; background-color: #F0F5F9; }"
        "QTabBar::tab { background-color: #E4E8ED; border: 1px solid #C9D6DF;"
        "  padding: 6px 14px; margin-right: 2px;"
        "  border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background-color: #F0F5F9; font-weight: bold; }"
        "QComboBox, QLineEdit { background-color: white; border: 1px solid #C9D6DF;"
        "  border-radius: 3px; padding: 2px 5px; }"
        "QGroupBox { background-color: #F0F5F9; border: 1px solid #C9D6DF;"
        "  border-radius: 5px; margin-top: 5px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px;"
        "  padding: 0 5px; background-color: #F0F5F9; }"
        "QTableView { background-color: white; alternate-background-color: #F9FBFC;"
        "  border: 1px solid #C9D6DF; gridline-color: #E4E8ED; }"
        "QTableView::item:selected { background-color: #0078D4; color: white; }"
        "QHeaderView::section { background-color: #E4E8ED; padding: 4px;"
        "  border: 1px solid #C9D6DF; font-weight: bold; }"
        "QPushButton { background-color: #52A7E0; border: none; border-radius: 4px;"
        "  padding: 6px 12px; font-weight: bold; color: white; min-width: 60px; }"
        "QPushButton:hover { background-color: #3D8BC7; }"
        "QPushButton:disabled { background-color: #C9D6DF; color: #6c757d; }"
        "QPushButton#dangerBtn { background-color: #E53935; }"
        "QPushButton#dangerBtn:hover { background-color: #C62828; }"
        "QPushButton#saveBtn { background-color: #43A047; }"
        "QPushButton#saveBtn:hover { background-color: #2E7D32; }"
        "QTextEdit { background-color: white; border: 1px solid #C9D6DF; }"
        "QListWidget { background-color: white; border: 1px solid #C9D6DF; }"
        "QSplitter::handle { background-color: #C9D6DF; }"
        "QStatusBar { background-color: #E4E8ED; }"
    );

    setupUI();
    connectSignals();

    // Centre window
    if (QScreen *screen = QApplication::primaryScreen()) {
        QRect sg = screen->availableGeometry();
        move((sg.width() - width()) / 2, (sg.height() - height()) / 2);
    }

    // Auto-load DSSAT config
    if (QFile::exists(Config::DSSATPRO_FILE))
        loadDssatConfig(Config::DSSAT_BASE);
}

MainWindow::~MainWindow() {}

// ─── close event ────────────────────────────────────────────────────────────
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Flush any pending auto-save before closing
    if (m_autoSaveTimer && m_autoSaveTimer->isActive()) {
        m_autoSaveTimer->stop();
        autoSaveAll();
    }
    event->accept();
}

// ─── UI setup ────────────────────────────────────────────────────────────────
void MainWindow::setupUI()
{
    setupMenuBar();

    // Central widget with vertical layout
    QWidget *central = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(8, 8, 8, 4);
    vbox->setSpacing(6);
    setCentralWidget(central);

    // ── Top bar: DSSAT dir + crop selector ──────────────────────────────────
    setupCropBar();
    vbox->addWidget(new QWidget(this));   // placeholder (bar added in setupCropBar)

    // Rebuild central after crop bar
    QGroupBox *topGroup = new QGroupBox("DSSAT Configuration", central);
    QGridLayout *topGrid = new QGridLayout(topGroup);
    topGrid->setHorizontalSpacing(8);

    topGrid->addWidget(new QLabel("DSSAT directory:"), 0, 0);
    m_dssatDirEdit = new QLineEdit(Config::DSSAT_BASE);
    topGrid->addWidget(m_dssatDirEdit, 0, 1);
    m_browseButton = new QPushButton("Browse…");
    m_browseButton->setFixedWidth(80);
    topGrid->addWidget(m_browseButton, 0, 2);

    topGrid->addWidget(new QLabel("Crop:"), 1, 0);
    m_cropCombo = new QComboBox;
    m_cropCombo->setMinimumWidth(280);
    topGrid->addWidget(m_cropCombo, 1, 1);
    topGrid->setColumnStretch(1, 1);

    m_geneticsLabel = new QLabel("—");
    m_geneticsLabel->setStyleSheet("color: #555; font-size: 10px;");
    topGrid->addWidget(m_geneticsLabel, 2, 0, 1, 3);

    // Replace placeholder
    auto *placeholderItem = vbox->takeAt(0);
    if (placeholderItem) {
        delete placeholderItem->widget();
        delete placeholderItem;
    }
    vbox->insertWidget(0, topGroup);

    // ── Tab widget ───────────────────────────────────────────────────────────
    m_tabWidget = new QTabWidget(central);
    vbox->addWidget(m_tabWidget, 1);

    QWidget *culTab = new QWidget;
    setupCulTab(culTab);
    m_tabWidget->addTab(culTab, "CUL — Cultivar");

    QWidget *ecoTab = new QWidget;
    setupEcoTab(ecoTab);
    m_tabWidget->addTab(ecoTab, "ECO — Ecotype");

    QWidget *speTab = new QWidget;
    setupSpeTab(speTab);
    m_tabWidget->addTab(speTab, "SPE — Species");

    // ── Status bar ───────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Ready");
    statusBar()->addPermanentWidget(m_statusLabel, 1);
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Open DSSAT directory…", this, &MainWindow::onOpenDssatDir);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", qApp, &QApplication::quit);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainWindow::onAbout);
}

void MainWindow::setupCropBar() {}  // logic merged into setupUI

static QPushButton *makeBtn(const QString &text, const QString &objName = {})
{
    auto *btn = new QPushButton(text);
    if (!objName.isEmpty()) btn->setObjectName(objName);
    return btn;
}

void MainWindow::setupCulTab(QWidget *tab)
{
    QVBoxLayout *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(4, 4, 4, 4);

    // Toolbar row
    QHBoxLayout *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Search:"));
    m_culSearch = new QLineEdit;
    m_culSearch->setPlaceholderText("Filter by VAR# or VRNAME…");
    m_culSearch->setMaximumWidth(260);
    toolbar->addWidget(m_culSearch);
    toolbar->addStretch();

    m_culAddBtn      = makeBtn("Add");
    m_culDelBtn      = makeBtn("Delete",    "dangerBtn");
    m_culDupBtn      = makeBtn("Duplicate");
    m_culSaveBtn     = makeBtn("Save",      "saveBtn");
    m_culExportBtn   = makeBtn("Export CSV");
    m_culImportBtn   = makeBtn("Import CSV");
    m_culValidateBtn = makeBtn("Validate");
    QPushButton *culGlueBtn = makeBtn("Paste GLUE");
    culGlueBtn->setToolTip("Paste a GLUE-calibrated cultivar line to update or add a row");
    connect(culGlueBtn, &QPushButton::clicked, this, &MainWindow::onCulPasteGlue);

    for (auto *b : {m_culAddBtn, m_culDelBtn, m_culDupBtn, m_culSaveBtn,
                    m_culExportBtn, m_culImportBtn, m_culValidateBtn, culGlueBtn})
        toolbar->addWidget(b);

    vbox->addLayout(toolbar);

    // Table view with sort/filter proxy
    m_culProxy = new CulSortProxy(this);
    m_culProxy->setSourceModel(m_culModel);
    m_culProxy->setFilterKeyColumn(-1);
    m_culProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_culView = new QTableView;
    m_culView->setModel(m_culProxy);
    m_culView->setAlternatingRowColors(true);
    m_culView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_culView->horizontalHeader()->setStretchLastSection(false);
    m_culView->verticalHeader()->setDefaultSectionSize(22);
    m_culView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_culView->setSortingEnabled(true);
    vbox->addWidget(m_culView, 1);

    new QShortcut(QKeySequence::Copy, m_culView, this, &MainWindow::onCulCopyRow);
}

void MainWindow::setupEcoTab(QWidget *tab)
{
    QVBoxLayout *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Search:"));
    m_ecoSearch = new QLineEdit;
    m_ecoSearch->setPlaceholderText("Filter by ECO# or ECONAME…");
    m_ecoSearch->setMaximumWidth(260);
    toolbar->addWidget(m_ecoSearch);
    toolbar->addStretch();

    m_ecoAddBtn  = makeBtn("Add");
    m_ecoDelBtn  = makeBtn("Delete",    "dangerBtn");
    m_ecoDupBtn  = makeBtn("Duplicate");
    m_ecoSaveBtn = makeBtn("Save",      "saveBtn");

    for (auto *b : {m_ecoAddBtn, m_ecoDelBtn, m_ecoDupBtn, m_ecoSaveBtn})
        toolbar->addWidget(b);

    vbox->addLayout(toolbar);

    m_ecoProxy = new EcoSortProxy(this);
    m_ecoProxy->setSourceModel(m_ecoModel);
    m_ecoProxy->setFilterKeyColumn(-1);
    m_ecoProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_ecoView = new QTableView;
    m_ecoView->setModel(m_ecoProxy);
    m_ecoView->setAlternatingRowColors(true);
    m_ecoView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ecoView->verticalHeader()->setDefaultSectionSize(22);
    m_ecoView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ecoView->setSortingEnabled(true);
    vbox->addWidget(m_ecoView, 1);

    new QShortcut(QKeySequence::Copy, m_ecoView, this, &MainWindow::onEcoCopyRow);
}

void MainWindow::setupSpeTab(QWidget *tab)
{
    QHBoxLayout *hbox = new QHBoxLayout(tab);
    hbox->setContentsMargins(4, 4, 4, 4);

    // Left: section navigator
    QVBoxLayout *navLayout = new QVBoxLayout;
    navLayout->addWidget(new QLabel("Sections:"));
    m_speNavList = new QListWidget;
    m_speNavList->setMaximumWidth(240);
    m_speNavList->setMinimumWidth(180);
    m_speNavList->setSpacing(2);
    m_speNavList->setStyleSheet(
        "QListWidget::item { padding: 4px 8px; border-radius: 3px; }"
        "QListWidget::item:selected { background: #1565C0; color: white; }"
        "QListWidget::item:hover:!selected { background: #E3F2FD; }"
    );
    navLayout->addWidget(m_speNavList, 1);
    hbox->addLayout(navLayout);

    // Right: text editor + toolbar
    QVBoxLayout *editLayout = new QVBoxLayout;

    QHBoxLayout *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Find:"));
    m_speSearchEdit = new QLineEdit;
    m_speSearchEdit->setPlaceholderText("Search in text…");
    m_speSearchEdit->setMaximumWidth(240);
    toolbar->addWidget(m_speSearchEdit);
    QPushButton *findBtn = new QPushButton("Find");
    toolbar->addWidget(findBtn);
    toolbar->addStretch();
    m_speSaveBtn = makeBtn("Save", "saveBtn");
    toolbar->addWidget(m_speSaveBtn);
    editLayout->addLayout(toolbar);

    m_speEdit = new QTextEdit;
    m_speEdit->setFont(QFont("Courier New", 9));
    m_speEdit->setLineWrapMode(QTextEdit::NoWrap);
    new SpeSyntaxHighlighter(m_speEdit->document());
    editLayout->addWidget(m_speEdit, 1);
    hbox->addLayout(editLayout, 1);

    // Connect find button
    connect(findBtn, &QPushButton::clicked, this, &MainWindow::onSpeSearch);
}

// ─── signal wiring ────────────────────────────────────────────────────────────
void MainWindow::connectSignals()
{
    // Auto-save timer: 800 ms after the last change, save all dirty files
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(800);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveAll);

    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onOpenDssatDir);
    connect(m_cropCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCropChanged);

    // CUL
    connect(m_culAddBtn,      &QPushButton::clicked, this, &MainWindow::onCulAdd);
    connect(m_culDelBtn,      &QPushButton::clicked, this, &MainWindow::onCulDelete);
    connect(m_culDupBtn,      &QPushButton::clicked, this, &MainWindow::onCulDuplicate);
    connect(m_culSaveBtn,     &QPushButton::clicked, this, &MainWindow::onCulSave);
    connect(m_culExportBtn,   &QPushButton::clicked, this, &MainWindow::onCulExportCsv);
    connect(m_culImportBtn,   &QPushButton::clicked, this, &MainWindow::onCulImportCsv);
    connect(m_culValidateBtn, &QPushButton::clicked, this, &MainWindow::onCulValidate);
    connect(m_culSearch,      &QLineEdit::textChanged, this, &MainWindow::onCulSearch);
    connect(m_culModel,       &CulTableModel::dataModified,
            [this](){ m_culDirty = true; setStatus("CUL modified…"); m_autoSaveTimer->start(); });

    // ECO
    connect(m_ecoAddBtn,  &QPushButton::clicked, this, &MainWindow::onEcoAdd);
    connect(m_ecoDelBtn,  &QPushButton::clicked, this, &MainWindow::onEcoDelete);
    connect(m_ecoDupBtn,  &QPushButton::clicked, this, &MainWindow::onEcoDuplicate);
    connect(m_ecoSaveBtn, &QPushButton::clicked, this, &MainWindow::onEcoSave);
    connect(m_ecoSearch,  &QLineEdit::textChanged, this, &MainWindow::onEcoSearch);
    connect(m_ecoModel,   &EcoTableModel::dataModified,
            [this](){ m_ecoDirty = true; setStatus("ECO modified…"); m_autoSaveTimer->start(); });

    // SPE
    connect(m_speSaveBtn, &QPushButton::clicked, this, &MainWindow::onSpeSave);
    connect(m_speEdit,    &QTextEdit::textChanged,
            [this](){ m_speDirty = true; setStatus("SPE modified…"); m_autoSaveTimer->start(); });
    connect(m_speNavList, &QListWidget::itemClicked,
            [this](QListWidgetItem *item){ onSpeSectionClicked(item->text()); });
    connect(m_speEdit->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MainWindow::onSpeScrolled);
}

// ─── DSSAT config loading ──────────────────────────────────────────────────
void MainWindow::loadDssatConfig(const QString &dssatDir)
{
    m_dssatDirEdit->setText(dssatDir);

    // Parse DSSATPRO
    QString proPath = dssatDir + "/DSSATPRO.v48";
#ifdef Q_OS_WIN
    proPath.replace('/', '\\');
#endif

    m_crops = DssatProParser::discoverCrops(proPath);

    // Parse DETAIL.CDE
    QString cdePath = dssatDir + "/DETAIL.CDE";
#ifdef Q_OS_WIN
    cdePath.replace('/', '\\');
#endif
    m_cdeData = DetailCdeParser::parse(cdePath);

    // Populate crop combo
    m_cropCombo->blockSignals(true);
    m_cropCombo->clear();
    for (auto it = m_crops.begin(); it != m_crops.end(); ++it) {
        const CropInfo &info = it.value();
        QString display = QString("%1 (%2) — %3")
                              .arg(info.cropCode, it.key(), info.description);
        m_cropCombo->addItem(display, it.key());
    }
    m_cropCombo->blockSignals(false);

    if (m_cropCombo->count() > 0) {
        m_cropCombo->setCurrentIndex(0);
        onCropChanged(0);  // explicitly load the first crop (signal was blocked during fill)
    } else {
        setStatus("No crops found in " + proPath, true);
    }
}

void MainWindow::onCropChanged(int index)
{
    if (index < 0) return;
    QString code = m_cropCombo->itemData(index).toString();
    if (!m_crops.contains(code)) return;
    loadCrop(code);
}

void MainWindow::loadCrop(const QString &cropCode)
{
    if (!m_crops.contains(cropCode)) return;
    const CropInfo &info = m_crops[cropCode];
    m_currentCropCode = cropCode;
    m_currentCulPath  = info.culFile;
    m_currentEcoPath  = info.ecoFile;
    m_currentSpePath  = info.speFile;

    m_geneticsLabel->setText(
        QString("CUL: %1   ECO: %2   SPE: %3")
            .arg(QFileInfo(info.culFile).fileName(),
                 QFileInfo(info.ecoFile).fileName(),
                 QFileInfo(info.speFile).fileName())
    );

    // Load CUL
    m_culHeaderLines.clear();
    QVector<CulRow> culRows = CulParser::parse(m_currentCulPath, m_culHeaderLines);
    m_culModel->setRows(culRows);
    m_culModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_culHeaderLines));
    m_culModel->setCalibrationTypes(CulParser::calibrationTypes(m_culHeaderLines));
    m_culDirty = false;

    // Load ECO
    m_ecoHeaderLines.clear();
    QVector<EcoRow> ecoRows = EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines);
    m_ecoModel->setRows(ecoRows);
    m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
    m_ecoDirty = false;

    refreshEcoCrossRef();

    // Load SPE
    QString speText = SpeEditor::load(m_currentSpePath);
    m_speEdit->blockSignals(true);
    m_speEdit->setPlainText(speText);
    m_speEdit->blockSignals(false);
    m_speDirty = false;
    buildSpeNavigator();

    setStatus(QString("Loaded %1 (%2) — %3 cultivars, %4 ecotypes")
              .arg(info.cropCode, cropCode)
              .arg(culRows.size())
              .arg(ecoRows.size()));
}

void MainWindow::refreshEcoCrossRef()
{
    QMap<QString, int> refs;
    const auto &culRows = m_culModel->rows();
    for (const auto &r : culRows) {
        if (!r.isMinMax)
            refs[r.ecoNum]++;
    }
    m_ecoModel->setCulCrossRef(refs);
}

void MainWindow::buildSpeNavigator()
{
    m_speNavList->clear();
    QStringList sections = SpeEditor::sectionNames(m_speEdit->toPlainText());
    for (const QString &s : sections)
        m_speNavList->addItem(s);
}

// ─── CUL actions ─────────────────────────────────────────────────────────────
void MainWindow::onCulAdd()
{
    m_culModel->addRow();
}

void MainWindow::onCulDelete()
{
    QModelIndex idx = m_culProxy->mapToSource(m_culView->currentIndex());
    if (!idx.isValid()) return;

    auto btn = QMessageBox::question(this, "Delete cultivar",
        "Delete selected cultivar row?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn == QMessageBox::Yes)
        m_culModel->deleteRow(idx.row());
}

void MainWindow::onCulDuplicate()
{
    QModelIndex idx = m_culProxy->mapToSource(m_culView->currentIndex());
    if (idx.isValid())
        m_culModel->duplicateRow(idx.row());
}

void MainWindow::onCulSave()
{
    if (m_currentCulPath.isEmpty()) return;

    // Backup
    BackupManager::createBackup(m_currentCulPath);
    BackupManager::pruneBackups(m_currentCulPath);

    if (CulParser::write(m_currentCulPath, m_culModel->rows(), m_culHeaderLines)) {
        m_culDirty = false;
        setStatus("CUL saved: " + m_currentCulPath);
    } else {
        setStatus("Failed to save: " + m_currentCulPath, true);
    }
}

void MainWindow::onCulCopyRow()
{
    QModelIndex idx = m_culProxy->mapToSource(m_culView->currentIndex());
    if (!idx.isValid()) return;
    const CulRow &row = m_culModel->rows().at(idx.row());
    QApplication::clipboard()->setText(CulParser::formatRow(row));
    setStatus(QString("Copied cultivar %1 to clipboard").arg(row.varNum));
}

void MainWindow::onEcoCopyRow()
{
    QModelIndex idx = m_ecoProxy->mapToSource(m_ecoView->currentIndex());
    if (!idx.isValid()) return;
    const EcoRow &row = m_ecoModel->rows().at(idx.row());
    QApplication::clipboard()->setText(EcoParser::formatRow(row));
    setStatus(QString("Copied ecotype %1 to clipboard").arg(row.ecoNum));
}

void MainWindow::onCulPasteGlue()
{
    bool ok;
    QString line = QInputDialog::getText(
        this, "Paste GLUE Calibrated Result",
        "Paste the cultivar line from GLUE output:",
        QLineEdit::Normal, QString(), &ok);
    if (!ok || line.trimmed().isEmpty()) return;

    CulRow newRow = CulParser::parseLine(line);
    if (newRow.varNum.isEmpty()) {
        QMessageBox::warning(this, "Paste GLUE",
            "Could not parse the pasted line.\n"
            "Expected format:\n"
            "  VAR#   VRNAME        EXPNO . ECO#   P1 P2 ...");
        return;
    }

    // Search for an existing row with the same VAR#
    const QVector<CulRow> &rows = m_culModel->rows();
    int existingRow = -1;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].varNum == newRow.varNum) { existingRow = i; break; }
    }

    if (existingRow >= 0) {
        auto btn = QMessageBox::question(this, "Paste GLUE",
            QString("Cultivar '%1' (%2) already exists.\nUpdate it with the GLUE values?")
                .arg(newRow.varNum, newRow.vrName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (btn != QMessageBox::Yes) return;

        // Update each parameter column via setData so dataModified is emitted
        QAbstractItemModel *src = m_culModel;
        auto idx = [&](int col){ return src->index(existingRow, col); };
        src->setData(idx(CulTableModel::COL_VRNAME), newRow.vrName);
        src->setData(idx(CulTableModel::COL_EXPNO),  newRow.expNo);
        src->setData(idx(CulTableModel::COL_ECONUM), newRow.ecoNum);
        for (int p = 0; p < newRow.params.size(); ++p)
            src->setData(idx(CulTableModel::COL_PARAM0 + p), newRow.params[p]);

        // Scroll the view to the updated row
        QModelIndex proxyIdx = m_culProxy->mapFromSource(m_culModel->index(existingRow, 0));
        m_culView->scrollTo(proxyIdx);
        m_culView->setCurrentIndex(proxyIdx);
        setStatus(QString("Updated cultivar %1 from GLUE result").arg(newRow.varNum));
    } else {
        // Add as a new row
        newRow.isMinMax = false;
        int newIdx = m_culModel->rows().size();
        m_culModel->addRow();  // appends a blank row
        // Now overwrite it with parsed values
        QAbstractItemModel *src = m_culModel;
        auto idx = [&](int col){ return src->index(newIdx, col); };
        src->setData(idx(CulTableModel::COL_VARNUM), newRow.varNum);
        src->setData(idx(CulTableModel::COL_VRNAME), newRow.vrName);
        src->setData(idx(CulTableModel::COL_EXPNO),  newRow.expNo);
        src->setData(idx(CulTableModel::COL_ECONUM), newRow.ecoNum);
        for (int p = 0; p < newRow.params.size(); ++p)
            src->setData(idx(CulTableModel::COL_PARAM0 + p), newRow.params[p]);

        QModelIndex proxyIdx = m_culProxy->mapFromSource(m_culModel->index(newIdx, 0));
        m_culView->scrollTo(proxyIdx);
        m_culView->setCurrentIndex(proxyIdx);
        setStatus(QString("Added new cultivar %1 from GLUE result").arg(newRow.varNum));
    }
}

void MainWindow::onCulExportCsv()
{
    QString path = QFileDialog::getSaveFileName(this, "Export CUL as CSV", {}, "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus("Cannot write: " + path, true); return;
    }
    QTextStream out(&f);

    // Header
    QStringList cols;
    for (int c = 0; c < m_culModel->columnCount(); ++c)
        cols << m_culModel->columnName(c);
    out << cols.join(",") << "\n";

    // Rows
    const auto &rows = m_culModel->rows();
    for (const auto &row : rows) {
        QStringList vals;
        vals << row.varNum << row.vrName << row.expNo << row.ecoNum;
        for (double v : row.params)
            vals << QString::number(v);
        out << vals.join(",") << "\n";
    }
    setStatus("Exported: " + path);
}

void MainWindow::onCulImportCsv()
{
    QString path = QFileDialog::getOpenFileName(this, "Import CSV into CUL", {}, "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus("Cannot read: " + path, true); return;
    }

    QTextStream in(&f);
    QString headerLine = in.readLine();   // skip header
    Q_UNUSED(headerLine);

    int imported = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(',');
        if (parts.size() < 4) continue;

        CulRow row;
        row.varNum = parts[0].left(6).trimmed();
        row.vrName = parts[1].left(13).trimmed();
        row.expNo  = parts[2].left(1).trimmed();
        row.ecoNum = parts[3].left(6).trimmed();
        for (int i = 4; i < parts.size() && row.params.size() < 18; ++i)
            row.params << parts[i].toDouble();
        while (row.params.size() < 18) row.params << 0.0;

        // Check if varNum already exists -> overwrite, else add
        m_culModel->addRow();
        QVector<CulRow> updatedRows = m_culModel->rows();
        updatedRows.last() = row;
        m_culModel->setRows(updatedRows);
        ++imported;
    }
    setStatus(QString("Imported %1 rows from CSV").arg(imported));
    m_culDirty = true;
}

void MainWindow::onCulValidate()
{
    const auto &rows = m_culModel->rows();
    QStringList issues;

    // Get ECO# set for cross-reference
    QStringList ecoNums;
    for (const auto &er : m_ecoModel->rows())
        if (!er.isMinMax) ecoNums << er.ecoNum;

    for (const auto &row : rows) {
        if (row.isMinMax) continue;

        if (row.varNum.trimmed().isEmpty())
            issues << row.varNum + ": empty VAR#";
        if (row.vrName.trimmed().isEmpty())
            issues << row.varNum + ": empty VRNAME";
        if (row.varNum != "DFAULT" && !ecoNums.contains(row.ecoNum))
            issues << row.varNum + ": ECO# '" + row.ecoNum + "' not found in ECO file";

        for (int i = 0; i < row.params.size(); ++i) {
            // Basic NaN/Inf check
            double v = row.params[i];
            if (!std::isfinite(v))
                issues << row.varNum + ": param " + CUL_PARAM_NAMES[i] + " is not finite";
        }
    }

    if (issues.isEmpty()) {
        QMessageBox::information(this, "Validation", "All checks passed — no issues found.");
    } else {
        QString msg = QString("%1 issue(s) found:\n\n").arg(issues.size());
        msg += issues.mid(0, 50).join("\n");
        if (issues.size() > 50) msg += "\n… and more";
        QMessageBox::warning(this, "Validation Issues", msg);
    }
}

void MainWindow::onCulSearch(const QString &text)
{
    if (m_culProxy)
        m_culProxy->setFilterFixedString(text);
}

// ─── ECO actions ─────────────────────────────────────────────────────────────
void MainWindow::onEcoAdd()  { m_ecoModel->addRow(); m_ecoDirty = true; }

void MainWindow::onEcoDelete()
{
    QModelIndex idx = m_ecoProxy->mapToSource(m_ecoView->currentIndex());
    if (!idx.isValid()) return;

    QString ecoNum = m_ecoModel->rows()[idx.row()].ecoNum;
    int refs = 0;
    for (const auto &cr : m_culModel->rows())
        if (cr.ecoNum == ecoNum) ++refs;

    if (refs > 0) {
        auto btn = QMessageBox::warning(this, "Delete ecotype",
            QString("%1 cultivar(s) still reference ECO# '%2'.\nDelete anyway?")
                .arg(refs).arg(ecoNum),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
    }
    m_ecoModel->deleteRow(idx.row());
}

void MainWindow::onEcoDuplicate()
{
    QModelIndex idx = m_ecoProxy->mapToSource(m_ecoView->currentIndex());
    if (idx.isValid()) m_ecoModel->duplicateRow(idx.row());
}

void MainWindow::onEcoSave()
{
    if (m_currentEcoPath.isEmpty()) return;
    BackupManager::createBackup(m_currentEcoPath);
    BackupManager::pruneBackups(m_currentEcoPath);
    if (EcoParser::write(m_currentEcoPath, m_ecoModel->rows(), m_ecoHeaderLines)) {
        m_ecoDirty = false;
        setStatus("ECO saved: " + m_currentEcoPath);
    } else {
        setStatus("Failed to save: " + m_currentEcoPath, true);
    }
}

void MainWindow::onEcoSearch(const QString &text)
{
    if (m_ecoProxy)
        m_ecoProxy->setFilterFixedString(text);
}

// ─── Auto-save ───────────────────────────────────────────────────────────────
void MainWindow::autoSaveAll()
{
    if (m_culDirty) onCulSave();
    if (m_ecoDirty) onEcoSave();
    if (m_speDirty) onSpeSave();
}

// ─── SPE actions ─────────────────────────────────────────────────────────────
void MainWindow::onSpeSave()
{
    if (m_currentSpePath.isEmpty()) return;
    BackupManager::createBackup(m_currentSpePath);
    BackupManager::pruneBackups(m_currentSpePath);
    if (SpeEditor::save(m_currentSpePath, m_speEdit->toPlainText())) {
        m_speDirty = false;
        setStatus("SPE saved: " + m_currentSpePath);
    } else {
        setStatus("Failed to save: " + m_currentSpePath, true);
    }
}

void MainWindow::onSpeSearch()
{
    QString needle = m_speSearchEdit->text().trimmed();
    if (needle.isEmpty()) return;
    QTextDocument *doc = m_speEdit->document();
    QTextCursor cur = m_speEdit->textCursor();
    cur = doc->find(needle, cur);
    if (cur.isNull()) {
        // Wrap around
        cur = doc->find(needle);
    }
    if (!cur.isNull()) {
        m_speEdit->setTextCursor(cur);
        m_speEdit->ensureCursorVisible();
    } else {
        setStatus("Text not found: " + needle, true);
    }
}

void MainWindow::onSpeSectionClicked(const QString &section)
{
    QString text = m_speEdit->toPlainText();
    int pos = SpeEditor::sectionOffset(text, section);
    if (pos < 0) return;

    QTextDocument *doc = m_speEdit->document();
    QTextBlock block = doc->findBlock(pos);

    // Place cursor at start of the section line
    QTextCursor cur(block);
    cur.movePosition(QTextCursor::StartOfLine);
    m_speEdit->setTextCursor(cur);

    // Highlight the section header line in light blue
    QTextEdit::ExtraSelection highlight;
    highlight.format.setBackground(QColor("#BBDEFB"));
    highlight.format.setProperty(QTextFormat::FullWidthSelection, true);
    highlight.cursor = cur;
    m_speEdit->setExtraSelections({ highlight });

    // Scroll so the section header appears near the top of the viewport (8px margin)
    QRectF blockRect = doc->documentLayout()->blockBoundingRect(block);
    m_speEdit->verticalScrollBar()->setValue(qMax(0, (int)blockRect.top() - 8));
}

void MainWindow::onSpeScrolled(int /*value*/)
{
    // Find the block currently at the top of the viewport
    QTextCursor cur = m_speEdit->cursorForPosition(QPoint(0, 0));
    QTextBlock block = cur.block();

    // Walk backwards to find the nearest section header above or at the top
    while (block.isValid()) {
        QString line = block.text().trimmed();
        bool isSection = line.startsWith("!*") ||
                         (line.startsWith('*') && !line.startsWith("**"));
        if (isSection) {
            QString name = line.startsWith("!*") ? line.mid(2).trimmed()
                                                 : line.mid(1).trimmed();
            for (int i = 0; i < m_speNavList->count(); ++i) {
                if (m_speNavList->item(i)->text() == name) {
                    m_speNavList->blockSignals(true);
                    m_speNavList->setCurrentRow(i);
                    m_speNavList->scrollToItem(m_speNavList->item(i),
                                               QAbstractItemView::EnsureVisible);
                    m_speNavList->blockSignals(false);
                    break;
                }
            }
            return;
        }
        block = block.previous();
    }

    // Above all sections — clear highlight
    m_speNavList->blockSignals(true);
    m_speNavList->clearSelection();
    m_speNavList->blockSignals(false);
}

// ─── Menu actions ─────────────────────────────────────────────────────────────
void MainWindow::onOpenDssatDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select DSSAT directory",
                                                    m_dssatDirEdit->text());
    if (!dir.isEmpty())
        loadDssatConfig(dir);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "About DSSAT Genetics Editor",
        QString("<b>DSSAT Genetics Editor v%1</b><br><br>"
                "Edit CUL, ECO, and SPE genetics files for all DSSAT crops.<br><br>"
                "Auto-discovers crop files via DSSATPRO.v48.<br><br>"
                "Built with Qt6 C++.")
            .arg(Config::APP_VERSION));
}

void MainWindow::setStatus(const QString &msg, bool error)
{
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(error
        ? "color: #C62828; font-weight: bold;"
        : "color: #1B5E20;");
}
