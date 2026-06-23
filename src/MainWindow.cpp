#include "MainWindow.h"
#include <QComboBox>
#include <QListView>

// QComboBox subclass that resizes the popup to a fixed grid after Qt shows it
class CropComboBox : public QComboBox {
public:
    explicit CropComboBox(QWidget *parent = nullptr) : QComboBox(parent) {}
    void showPopup() override {
        QComboBox::showPopup();
        // After Qt positions the popup, resize it to our grid dimensions
        if (QWidget *popup = this->findChild<QFrame*>()) {
            popup->setFixedWidth(180 * 4);   // 4 columns x 180px
            popup->setFixedHeight(26 * 13);  // 13 rows x 26px
            // Re-center below the combo button
            QPoint pos = mapToGlobal(QPoint(0, height()));
            popup->move(pos);
        }
    }
};
#include "CulParser.h"
#include "EcoParser.h"
#include "BackupManager.h"
#include "SpeEditor.h"
#include "SpeSyntaxHighlighter.h"
#include "Config.h"
#include "GlueRunner.h"
#include "GlueQueueDialog.h"
#include "GlueQueueManager.h"
#include "GlueQueuePanel.h"
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QLabel>
#include <QCloseEvent>
#include <QHeaderView>
#include <QFileInfo>
#include <optional>
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
#include <QSet>
#include <QDirIterator>
#include <QRegularExpression>
#include <algorithm>
#include <memory>
#include <QRegularExpression>

// ─── Proxy: pins MINIMA/MAXIMA rows to the top during any sort ───────────────
class CulSortProxy : public QSortFilterProxyModel {
public:
    explicit CulSortProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setUsedFilter(const QSet<QString> &usedVarNums) {
        m_usedVarNums = usedVarNums;
        m_filterActive = !usedVarNums.isEmpty();
        invalidateFilter();
    }

    void clearUsedFilter() {
        m_usedVarNums.clear();
        m_filterActive = false;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        if (m_filterActive) {
            QString varNum = sourceModel()->data(sourceModel()->index(sourceRow, 0, sourceParent)).toString().trimmed();
            if (varNum != "999991" && varNum != "999992" && !m_usedVarNums.contains(varNum))
                return false;
        }
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

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

private:
    QSet<QString> m_usedVarNums;
    bool          m_filterActive = false;
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

    // Resolve GLUE paths (QSettings → common locations → DSSATPRO DGL entry)
    GlueRunner::resolvePaths(Config::DSSATPRO_FILE);
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
    m_cropCombo = new CropComboBox;
    m_cropCombo->setMinimumWidth(280);
    {
        QListView *popupView = new QListView(m_cropCombo);
        popupView->setViewMode(QListView::ListMode);
        popupView->setFlow(QListView::TopToBottom);
        popupView->setWrapping(true);
        popupView->setResizeMode(QListView::Adjust);
        popupView->setUniformItemSizes(true);
        popupView->setGridSize(QSize(180, 26));
        popupView->setStyleSheet(
            "QListView { font-size: 13px; padding: 4px; }"
            "QListView::item { padding: 2px 8px; border-radius: 3px; }"
            "QListView::item:hover { background: #E3F2FD; color: #1565C0; }"
            "QListView::item:selected { background: #1976D2; color: white; font-weight: bold; }"
        );
        m_cropCombo->setView(popupView);
    }
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
    fileMenu->addAction("Set GLUE directory…",   this, &MainWindow::onOpenGlueDir);
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
    m_glueQueue = new GlueQueueManager(this);

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
    m_culRefreshBtn = makeBtn("Refresh");
    m_culShowUsedBtn = makeBtn("Show Used");
    m_culShowUsedBtn->setCheckable(true);
    m_culShowUsedBtn->setToolTip("Show only cultivars used in experiment files");
    QPushButton *culGlueBtn = makeBtn("Paste GLUE");
    culGlueBtn->setToolTip("Paste a GLUE-calibrated cultivar line to update or add a row");
    connect(culGlueBtn, &QPushButton::clicked, this, &MainWindow::onCulPasteGlue);

    m_culGlueQueueBtn = makeBtn("Run GLUE");
    QPushButton *addQueueBtn = m_culGlueQueueBtn;
    addQueueBtn->setToolTip("Select treatments and add this cultivar to the GLUE calibration queue");
    addQueueBtn->setStyleSheet("font-weight:bold; background:#2196F3; color:white;");
    connect(addQueueBtn, &QPushButton::clicked, this, [this]() {
        QModelIndex idx = m_culView->currentIndex();
        if (!idx.isValid()) {
            QMessageBox::warning(this, "GLUE Queue", "Please select a cultivar row first.");
            return;
        }
        QModelIndex srcIdx = m_culProxy->mapToSource(idx);
        QString varNum = m_culModel->data(m_culModel->index(srcIdx.row(), CulTableModel::COL_VARNUM)).toString().trimmed();
        QString vrName = m_culModel->data(m_culModel->index(srcIdx.row(), CulTableModel::COL_VRNAME)).toString().trimmed();
        if (varNum == "999991" || varNum == "999992") {
            QMessageBox::warning(this, "GLUE Queue", "Cannot run GLUE on MINIMA/MAXIMA rows.");
            return;
        }
        GlueQueueDialog dlg(m_crops[m_currentCropCode], varNum, vrName, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_glueQueue->addEntry(dlg.result());
            m_culGlueQueueBtn->setText("Add to GLUE Queue");
        }
    });

    for (auto *b : {m_culAddBtn, m_culDelBtn, m_culDupBtn, m_culSaveBtn,
                    m_culRefreshBtn, m_culShowUsedBtn, culGlueBtn, addQueueBtn})
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

    // Queue panel below table in a splitter
    m_gluePanel = new GlueQueuePanel(m_glueQueue, tab);
    m_gluePanel->setMaximumHeight(160);

    QSplitter *splitter = new QSplitter(Qt::Vertical, tab);
    splitter->addWidget(m_culView);
    splitter->addWidget(m_gluePanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    vbox->addWidget(splitter, 1);

    // Auto-apply calibrated results to CUL model
    connect(m_glueQueue, &GlueQueueManager::entryFinished,
            this, [this](int index, bool success, const QString &culLine) {
        if (!success || culLine.isEmpty()) {
            setStatus(QString("GLUE failed for entry %1").arg(index + 1), true);
            return;
        }
        const GlueQueueEntry &entry = m_glueQueue->entries().at(index);
        QString varNum = entry.cultivarId;

        // Find the row in the model
        int culRow = -1;
        const auto &rows = m_culModel->rows();
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].varNum.trimmed() == varNum) { culRow = i; break; }
        }
        if (culRow < 0) {
            setStatus(QString("GLUE done for %1 but cultivar not found in table").arg(varNum), true);
            return;
        }

        // Parse and apply params — fixed-width: VARNUM(6) SP VRNAME(16) EXPNO(7) ECO(6) SP params
        QString paramStr = culLine.mid(37).trimmed();
        QStringList vals = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        int nParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
        for (int i = 0; i < qMin(vals.size(), nParams); ++i) {
            bool ok = false;
            double v = vals[i].toDouble(&ok);
            if (ok)
                m_culModel->setData(m_culModel->index(culRow, CulTableModel::COL_PARAM0 + i), v);
        }
        setStatus(QString("GLUE calibration applied to %1 — click Save to write to file.").arg(varNum));
    });

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
    
    QSplitter *editSplitter = new QSplitter(Qt::Vertical);
    editSplitter->addWidget(m_speEdit);
    
    m_speGraphWidget = new SpeGraphWidget;
    editSplitter->addWidget(m_speGraphWidget);
    
    editSplitter->setStretchFactor(0, 3);
    editSplitter->setStretchFactor(1, 1);
    
    editLayout->addWidget(editSplitter, 1);
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
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index){
        if (m_currentCropCode.isEmpty()) return;
        if (index == 0 && !m_currentCulPath.isEmpty()) loadFileType("CUL");
        else if (index == 1 && !m_currentEcoPath.isEmpty()) loadFileType("ECO");
        else if (index == 2 && !m_currentSpePath.isEmpty()) loadFileType("SPE");
    });

    // CUL
    connect(m_culAddBtn,      &QPushButton::clicked, this, &MainWindow::onCulAdd);
    connect(m_culDelBtn,      &QPushButton::clicked, this, &MainWindow::onCulDelete);
    connect(m_culDupBtn,      &QPushButton::clicked, this, &MainWindow::onCulDuplicate);
    connect(m_culSaveBtn,     &QPushButton::clicked, this, &MainWindow::onCulSave);
    connect(m_culRefreshBtn,  &QPushButton::clicked, this, &MainWindow::onCulRefresh);
    connect(m_culShowUsedBtn, &QPushButton::toggled, this, &MainWindow::onCulShowUsed);
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
    connect(m_speEdit, &QTextEdit::cursorPositionChanged,
            this, &MainWindow::onSpeCursorPositionChanged);
}

// ─── DSSAT config loading ──────────────────────────────────────────────────
void MainWindow::loadDssatConfig(const QString &dssatDir)
{
    m_dssatDirEdit->setText(dssatDir);

    // Parse DSSATPRO — use the platform-specific file
#ifdef Q_OS_WIN
    QString proPath = dssatDir + "\\DSSATPRO.v48";
#else
    QString proPath = dssatDir + "/DSSATPRO.L48";
#endif

    m_crops = DssatProParser::discoverCrops(proPath);

    // Parse DETAIL.CDE
    QString cdePath = dssatDir + "/DETAIL.CDE";
#ifdef Q_OS_WIN
    cdePath.replace('/', '\\');
#endif
    m_cdeData = DetailCdeParser::parse(cdePath);

    // Populate crop combo — sorted by description, crop name first
    m_cropCombo->blockSignals(true);
    m_cropCombo->clear();

    // Build display name — append model suffix when multiple crops share the same name
    QList<QPair<QString, QString>> entries; // display, key
    QMap<QString, int> nameCount;
    for (auto it = m_crops.begin(); it != m_crops.end(); ++it) {
        const CropInfo &info = it.value();
        QString desc = info.description;
        int dash = desc.indexOf('-');
        if (dash >= 0) desc = desc.mid(dash + 1).trimmed();
        nameCount[desc]++;
    }
    for (auto it = m_crops.begin(); it != m_crops.end(); ++it) {
        const CropInfo &info = it.value();
        QString desc = info.description;
        int dash = desc.indexOf('-');
        if (dash >= 0) desc = desc.mid(dash + 1).trimmed();
        QString display = desc;
        if (nameCount[desc] > 1 && !info.module.isEmpty())
            display = QString("%1 (%2)").arg(desc, info.module);
        entries.append({display, it.key()});
    }
    std::sort(entries.begin(), entries.end(),
              [](const QPair<QString,QString> &a, const QPair<QString,QString> &b){
                  return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
              });
    for (const auto &e : entries)
        m_cropCombo->addItem(e.first, e.second);

    m_cropCombo->blockSignals(false);

    if (m_cropCombo->count() > 0) {
        m_cropCombo->setCurrentIndex(0);
        onCropChanged(0);
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

    m_geneticsLabel->setText("File: —");

    // Reset "Show Used" toggle when crop changes
    m_culShowUsedBtn->blockSignals(true);
    m_culShowUsedBtn->setChecked(false);
    m_culShowUsedBtn->setText("Show Used");
    m_culShowUsedBtn->blockSignals(false);
    m_culProxy->clearUsedFilter();
    if (m_culGlueQueueBtn) m_culGlueQueueBtn->setText("Run GLUE");

    setStatus(QString("Selected crop: %1 (%2)").arg(info.cropCode, cropCode));

    // Auto-load whichever file is available, starting with CUL
    if (QFileInfo::exists(info.culFile))
        loadFileType("CUL");
    else if (QFileInfo::exists(info.ecoFile))
        loadFileType("ECO");
    else if (QFileInfo::exists(info.speFile))
        loadFileType("SPE");
}

void MainWindow::loadFileType(const QString &fileType)
{
    if (m_currentCropCode.isEmpty() || !m_crops.contains(m_currentCropCode)) return;
    const CropInfo &info = m_crops[m_currentCropCode];

    if (fileType == "CUL") {
        m_culHeaderLines.clear();
        QVector<CulRow> culRows = CulParser::parse(m_currentCulPath, m_culHeaderLines);
        QStringList paramNames = CulParser::extractParamNames(m_culHeaderLines);
        m_culModel->setParamNames(paramNames.isEmpty() ? CUL_PARAM_NAMES : paramNames);
        m_culModel->setRows(culRows);
        m_culModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_culHeaderLines));
        m_culModel->setCalibrationTypes(CulParser::calibrationTypes(m_culHeaderLines));
        m_culDirty = false;
        m_tabWidget->setCurrentIndex(0);
        setStatus(QString("Loaded CUL: %1 — %2 cultivars").arg(QFileInfo(m_currentCulPath).fileName()).arg(culRows.size()));

        if (!m_currentEcoPath.isEmpty() && m_ecoModel->rows().isEmpty()) {
            m_ecoHeaderLines.clear();
            m_ecoModel->setRows(EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines));
            m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
            m_ecoDirty = false;
            refreshEcoCrossRef();
        }

        // Default to "Show Used" filter — silently, only if experiment files exist
        {
            const CropInfo &ci = m_crops.value(m_currentCropCode);
            QString xExt = ci.cropCode + "X";
            QDirIterator scanIt(ci.expDir,
                                QStringList() << "*." + xExt << "*." + xExt.toLower(),
                                QDir::Files, QDirIterator::Subdirectories);
            QSet<QString> usedVarNums;
            while (scanIt.hasNext()) {
                QFile f(scanIt.next());
                if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                QTextStream in(&f);
                bool inCulSection = false;
                int cuCol = -1, crCol = -1;
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    QString trimmed = line.trimmed();
                    if (trimmed.isEmpty() || trimmed.startsWith('!')) continue;
                    if (trimmed.startsWith("*CULTIVAR")) { inCulSection = true; cuCol = -1; continue; }
                    if (!inCulSection) continue;
                    if (trimmed.startsWith('*')) { inCulSection = false; continue; }
                    if (trimmed.startsWith("@C")) {
                        QStringList hdrs = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                        cuCol = hdrs.indexOf("INGENO");
                        crCol = hdrs.indexOf("CR");
                        continue;
                    }
                    if (trimmed.startsWith('@') || cuCol < 0) continue;
                    QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    if (parts.size() <= cuCol) continue;
                    if (crCol >= 0 && crCol < parts.size() &&
                        parts[crCol].compare(ci.cropCode, Qt::CaseInsensitive) != 0) continue;
                    usedVarNums.insert(parts[cuCol].trimmed());
                }
            }
            if (!usedVarNums.isEmpty()) {
                m_culProxy->setUsedFilter(usedVarNums);
                m_culShowUsedBtn->blockSignals(true);
                m_culShowUsedBtn->setChecked(true);
                m_culShowUsedBtn->setText("Show All");
                m_culShowUsedBtn->blockSignals(false);
                setStatus(QString("Loaded CUL: %1 — showing %2 cultivar(s) used in experiments.")
                              .arg(QFileInfo(m_currentCulPath).fileName()).arg(usedVarNums.size()));
            }
        }
    } else if (fileType == "ECO") {
        m_ecoHeaderLines.clear();
        QVector<EcoRow> ecoRows = EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines);
        m_ecoModel->setRows(ecoRows);
        m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
        m_ecoDirty = false;
        refreshEcoCrossRef();
        m_tabWidget->setCurrentIndex(1);
        setStatus(QString("Loaded ECO: %1 — %2 ecotypes").arg(QFileInfo(m_currentEcoPath).fileName()).arg(ecoRows.size()));
    } else if (fileType == "SPE") {
        QString speText = SpeEditor::load(m_currentSpePath);
        m_speEdit->blockSignals(true);
        m_speEdit->setPlainText(speText);
        m_speEdit->blockSignals(false);
        m_speDirty = false;
        buildSpeNavigator();
        m_tabWidget->setCurrentIndex(2);
        setStatus(QString("Loaded SPE: %1").arg(QFileInfo(m_currentSpePath).fileName()));
    }

    m_geneticsLabel->setText(
        QString("File: %1").arg(
            fileType == "CUL" ? QFileInfo(info.culFile).fileName() :
            fileType == "ECO" ? QFileInfo(info.ecoFile).fileName() :
                                QFileInfo(info.speFile).fileName())
    );
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
    bool ok;
    
    // Ask for VRNAME
    QString vrName = QInputDialog::getText(this, "New Cultivar",
        "Enter cultivar name (VRNAME):", QLineEdit::Normal,
        "NEW CULTIVAR", &ok);
    if (!ok || vrName.isEmpty()) return;
    
    // Ask for EXPNO
    QString expNo = QInputDialog::getText(this, "New Cultivar",
        "Enter experiment number (EXPNO):", QLineEdit::Normal,
        " ", &ok);
    if (!ok) return;
    
    // Ask for ECO#
    QString ecoNum = QInputDialog::getText(this, "New Cultivar",
        "Enter ecotype code (ECO#):", QLineEdit::Normal,
        "DFAULT", &ok);
    if (!ok || ecoNum.isEmpty()) return;
    
    // Ask for each numeric parameter with skip button
    int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
    QVector<std::optional<double>> params(numParams);
    
    for (int i = 0; i < numParams; ++i) {
        QDialog dialog(this);
        dialog.setWindowTitle("New Cultivar Parameters");
        
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        
        QString paramName = m_culModel->columnName(CulTableModel::COL_PARAM0 + i);
        QLabel *label = new QLabel("Enter " + paramName + ":");
        layout->addWidget(label);
        
        QLineEdit *lineEdit = new QLineEdit();
        lineEdit->setText("0");
        lineEdit->selectAll();
        layout->addWidget(lineEdit);
        
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *addBtn = new QPushButton("Add");
        QPushButton *skipBtn = new QPushButton("Skip");
        buttonLayout->addWidget(addBtn);
        buttonLayout->addWidget(skipBtn);
        layout->addLayout(buttonLayout);
        
        bool accepted = false;
        connect(addBtn, &QPushButton::clicked, [&]() {
            params[i] = lineEdit->text().toDouble();
            accepted = true;
            dialog.accept();
        });
        
        connect(skipBtn, &QPushButton::clicked, [&]() {
            // Leave remaining as nullopt (no value)
            dialog.reject();
        });
        
        int result = dialog.exec();
        if (result == QDialog::Rejected) {
            // Skip was clicked - remaining params stay as nullopt
            break;
        }
    }
    
    // Create row with all user-provided values
    int newRow = m_culModel->rowCount();
    m_culModel->addRowWithFullData(vrName, expNo, ecoNum, params);
    m_culDirty = true;
    
    // Focus on the new row
    QModelIndex newIdx = m_culProxy->mapFromSource(
        m_culModel->index(newRow, CulTableModel::COL_VARNUM));
    if (newIdx.isValid())
        m_culView->setCurrentIndex(newIdx);
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

    QStringList paramNames;
    int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
    for (int i = 0; i < numParams; ++i) {
        paramNames << m_culModel->columnName(CulTableModel::COL_PARAM0 + i);
    }

    if (CulParser::write(m_currentCulPath, m_culModel->rows(), m_culHeaderLines, paramNames)) {
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
    int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
    QVector<ParamFormat> fmts = CulParser::inferFormats(m_culModel->rows(), numParams);
    QApplication::clipboard()->setText(CulParser::formatRow(row, fmts, numParams));
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
        for (int p = 0; p < newRow.params.size(); ++p) {
            QVariant val = newRow.params[p].has_value() ? QVariant(newRow.params[p].value()) : QVariant();
            src->setData(idx(CulTableModel::COL_PARAM0 + p), val);
        }

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
        for (int p = 0; p < newRow.params.size(); ++p) {
            QVariant val = newRow.params[p].has_value() ? QVariant(newRow.params[p].value()) : QVariant();
            src->setData(idx(CulTableModel::COL_PARAM0 + p), val);
        }

        QModelIndex proxyIdx = m_culProxy->mapFromSource(m_culModel->index(newIdx, 0));
        m_culView->scrollTo(proxyIdx);
        m_culView->setCurrentIndex(proxyIdx);
        setStatus(QString("Added new cultivar %1 from GLUE result").arg(newRow.varNum));
    }
}


void MainWindow::onCulRefresh()
{
    // Reload CUL
    if (!m_currentCulPath.isEmpty()) {
        m_culHeaderLines.clear();
        QVector<CulRow> culRows = CulParser::parse(m_currentCulPath, m_culHeaderLines);
        QStringList paramNames = CulParser::extractParamNames(m_culHeaderLines);
        m_culModel->setParamNames(paramNames.isEmpty() ? CUL_PARAM_NAMES : paramNames);
        m_culModel->setRows(culRows);
        m_culModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_culHeaderLines));
        m_culModel->setCalibrationTypes(CulParser::calibrationTypes(m_culHeaderLines));
        m_culDirty = false;
    }

    // Reload ECO
    if (!m_currentEcoPath.isEmpty()) {
        m_ecoHeaderLines.clear();
        m_ecoModel->setRows(EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines));
        m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
        m_ecoDirty = false;
        refreshEcoCrossRef();
    }

    setStatus("Refreshed CUL and ECO from disk.");

    // Validate
    const auto &rows = m_culModel->rows();
    QStringList issues;

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
            if (row.params[i].has_value()) {
                double v = row.params[i].value();
                if (!std::isfinite(v))
                    issues << row.varNum + ": param " + CUL_PARAM_NAMES[i] + " is not finite";
            }
        }
    }

    if (issues.isEmpty()) {
        QMessageBox::information(this, "Refresh", "Files reloaded — no issues found.");
    } else {
        QString msg = QString("%1 issue(s) found:\n\n").arg(issues.size());
        msg += issues.mid(0, 50).join("\n");
        if (issues.size() > 50) msg += "\n… and more";
        QMessageBox::warning(this, "Refresh — Issues Found", msg);
    }
}

void MainWindow::onCulSearch(const QString &text)
{
    if (m_culProxy)
        m_culProxy->setFilterFixedString(text);
}

void MainWindow::onCulShowUsed(bool checked)
{
    if (!checked) {
        m_culProxy->clearUsedFilter();
        m_culShowUsedBtn->setText("Show Used");
        setStatus("Showing all cultivars.");
        return;
    }

    const CropInfo &info = m_crops.value(m_currentCropCode);
    if (info.expDir.isEmpty()) {
        m_culShowUsedBtn->blockSignals(true);
        m_culShowUsedBtn->setChecked(false);
        m_culShowUsedBtn->blockSignals(false);
        setStatus("No experiment directory configured — showing all cultivars.");
        return;
    }

    QString xExt = info.cropCode + "X";
    QDirIterator it(info.expDir,
                    QStringList() << "*." + xExt << "*." + xExt.toLower(),
                    QDir::Files, QDirIterator::Subdirectories);

    QSet<QString> usedVarNums;
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
        bool inCulSection = false;
        int cuCol = -1, crCol = -1;
        while (!in.atEnd()) {
            QString line = in.readLine();
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('!')) continue;
            if (trimmed.startsWith("*CULTIVAR")) { inCulSection = true; cuCol = -1; continue; }
            if (!inCulSection) continue;
            if (trimmed.startsWith('*')) { inCulSection = false; continue; }
            if (trimmed.startsWith("@C")) {
                QStringList hdrs = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                cuCol = hdrs.indexOf("INGENO");
                crCol = hdrs.indexOf("CR");
                continue;
            }
            if (trimmed.startsWith('@') || cuCol < 0) continue;
            QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() <= cuCol) continue;
            if (crCol >= 0 && crCol < parts.size() &&
                parts[crCol].compare(info.cropCode, Qt::CaseInsensitive) != 0) continue;
            usedVarNums.insert(parts[cuCol].trimmed());
        }
    }

    if (usedVarNums.isEmpty()) {
        m_culShowUsedBtn->blockSignals(true);
        m_culShowUsedBtn->setChecked(false);
        m_culShowUsedBtn->blockSignals(false);
        setStatus("No cultivars found in experiment files — showing all.");
        return;
    }

    m_culProxy->setUsedFilter(usedVarNums);
    m_culShowUsedBtn->setText("Show All");
    setStatus(QString("Showing %1 cultivar(s) used in experiments.").arg(usedVarNums.size()));
}

// ─── ECO actions ─────────────────────────────────────────────────────────────
void MainWindow::onEcoAdd()  
{ 
    bool ok;
    
    // Ask for ECONAME
    QString ecoName = QInputDialog::getText(this, "New Ecotype",
        "Enter ecotype name (ECONAME):", QLineEdit::Normal,
        "NEW ECOTYPE", &ok);
    if (!ok || ecoName.isEmpty()) return;
    
    // Ask for MG
    QString mg = QInputDialog::getText(this, "New Ecotype",
        "Enter maturity group (MG):", QLineEdit::Normal,
        " 0", &ok);
    if (!ok) return;
    
    // Ask for TM
    QString tm = QInputDialog::getText(this, "New Ecotype",
        "Enter temperature modifier (TM):", QLineEdit::Normal,
        " 0", &ok);
    if (!ok) return;
    
    // Ask for each numeric parameter with skip button
    QVector<std::optional<double>> params(16);
    
    for (int i = 0; i < ECO_PARAM_NAMES.size(); ++i) {
        QDialog dialog(this);
        dialog.setWindowTitle("New Ecotype Parameters");
        
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        
        QLabel *label = new QLabel("Enter " + ECO_PARAM_NAMES[i] + ":");
        layout->addWidget(label);
        
        QLineEdit *lineEdit = new QLineEdit();
        lineEdit->setText("0");
        lineEdit->selectAll();
        layout->addWidget(lineEdit);
        
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *addBtn = new QPushButton("Add");
        QPushButton *skipBtn = new QPushButton("Skip");
        buttonLayout->addWidget(addBtn);
        buttonLayout->addWidget(skipBtn);
        layout->addLayout(buttonLayout);
        
        bool accepted = false;
        connect(addBtn, &QPushButton::clicked, [&]() {
            params[i] = lineEdit->text().toDouble();
            accepted = true;
            dialog.accept();
        });
        
        connect(skipBtn, &QPushButton::clicked, [&]() {
            // Leave remaining as nullopt (no value)
            dialog.reject();
        });
        
        int result = dialog.exec();
        if (result == QDialog::Rejected) {
            // Skip was clicked - remaining params stay as nullopt
            break;
        }
    }
    
    // Create row with all user-provided values
    int newRow = m_ecoModel->rowCount();
    m_ecoModel->addRowWithFullData(ecoName, mg, tm, params);
    m_ecoDirty = true;
    
    // Focus on the new row
    QModelIndex newIdx = m_ecoProxy->mapFromSource(
        m_ecoModel->index(newRow, EcoTableModel::COL_ECONUM));
    if (newIdx.isValid())
        m_ecoView->setCurrentIndex(newIdx);
}

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

void MainWindow::onSpeCursorPositionChanged()
{
    QTextCursor cursor = m_speEdit->textCursor();
    QString line = cursor.block().text().trimmed();

    QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QVector<double> temps;
    
    for (const QString &part : parts) {
        bool ok;
        double val = part.toDouble(&ok);
        if (ok) {
            temps.append(val);
        } else {
            break;
        }
    }
    
    if (temps.size() >= 3 && temps.size() <= 10) {
        SpeGraphData data;
        
        QString paramName = "Parameter Curve";
        if (parts.size() > temps.size()) {
            paramName = parts.last();
        }
        
        data.title = QString("Graph: %1").arg(paramName);
        // Note: Using a pure generic label since some params are non-temp
        data.xAxisLabel = "Parameter Value";
        data.yAxisLabel = "Relative Effect Y";
        
        if (temps.size() == 4) {
             data.points.append(QPointF(temps[0], 0.0));
             data.points.append(QPointF(temps[1], 1.0));
             data.points.append(QPointF(temps[2], 1.0));
             data.points.append(QPointF(temps[3], 0.0));
        } else {
             QTextBlock nextBlock = cursor.block().next();
             if (nextBlock.isValid()) {
                 QString nextLine = nextBlock.text().trimmed();
                 QStringList nextParts = nextLine.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                 QVector<double> yVals;
                 for (const QString &p : nextParts) {
                     bool ok; double y = p.toDouble(&ok);
                     if (ok) yVals.append(y); else break;
                 }
                 if (yVals.size() == temps.size()) {
                     for (int i=0; i<temps.size(); i++) {
                         data.points.append(QPointF(temps[i], yVals[i]));
                     }
                 } else {
                     m_speGraphWidget->clearData();
                     return;
                 }
             }
        }
        m_speGraphWidget->setData(data);
    } else {
        m_speGraphWidget->clearData();
    }
}

// ─── Menu actions ─────────────────────────────────────────────────────────────
void MainWindow::onOpenDssatDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select DSSAT directory",
                                                    m_dssatDirEdit->text());
    if (!dir.isEmpty())
        loadDssatConfig(dir);
}

void MainWindow::onOpenGlueDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select GLUE software directory",
                                                    GlueRunner::GLUE_DIR);
    if (dir.isEmpty()) return;
    if (!QFile::exists(dir + "/SimulationControl.csv")) {
        QMessageBox::warning(this, "GLUE directory",
            "SimulationControl.csv not found in:\n" + dir +
            "\n\nPlease select the folder containing your GLUE R scripts.");
        return;
    }
    GlueRunner::setGlueDir(dir);
    setStatus("GLUE directory set: " + dir);
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
