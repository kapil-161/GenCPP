#include "MainWindow.h"
#include <QComboBox>
#include <QListView>
#include <QScreen>
#include <algorithm>

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
            // Centred below the combo, kept on screen (the combo sits on the right
            // half of the window, so the 720px grid could run past the screen edge)
            QPoint pos = mapToGlobal(QPoint(width() / 2 - popup->width() / 2, height()));
            if (const QScreen *scr = screen()) {
                const QRect area = scr->availableGeometry();
                if (pos.x() < area.left()) pos.setX(area.left());
                if (pos.x() + popup->width() > area.right())
                    pos.setX(std::max(area.left(), area.right() - popup->width()));
                if (pos.y() + popup->height() > area.bottom())    // no room below: open above
                    pos.setY(std::max(area.top(), mapToGlobal(QPoint(0, 0)).y() - popup->height()));
            }
            popup->move(pos);
        }
    }
};
#include "CulParser.h"
#include "EcoParser.h"
#include "BackupManager.h"
#include "SpeEditor.h"
#include "SpeSyntaxHighlighter.h"
#include "SpeGraphBuilder.h"
#include "FrozenTableView.h"
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
#include <QMenu>
#include <QAbstractTextDocumentLayout>
#include <QTimer>
#include <QVariantAnimation>
#include <QDebug>
#include <QSet>
#include <QDirIterator>
#include <QRegularExpression>
#include <algorithm>
#include <memory>
#include <functional>
#include <QRegularExpression>
#include <QStyledItemDelegate>

// ─── ECO# editor: pick one of the crop's ecotypes ───────────────────────────
namespace {
class EcoComboDelegate : public QStyledItemDelegate
{
public:
    using EcoList = QVector<QPair<QString, QString>>;   // ECO#, ECONAME
    EcoComboDelegate(std::function<EcoList()> ecotypes, QObject *parent)
        : QStyledItemDelegate(parent), m_ecotypes(std::move(ecotypes)) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override
    {
        const EcoList list = m_ecotypes();
        if (list.isEmpty())   // no ECO file: type the code as before
            return QStyledItemDelegate::createEditor(parent, opt, index);
        auto *combo = new QComboBox(parent);
        for (const auto &e : list)
            combo->addItem(e.second.isEmpty() ? e.first : e.first + "  " + e.second, e.first);
        // A wide list is easier to read than one clipped to the column width
        combo->view()->setMinimumWidth(combo->view()->sizeHintForColumn(0) + 24);
        // Picking from the list is the whole edit: save and close at once
        auto *self = const_cast<EcoComboDelegate *>(this);
        connect(combo, QOverload<int>::of(&QComboBox::activated), self, [self, combo]() {
            emit self->commitData(combo);
            emit self->closeEditor(combo);
        });
        QTimer::singleShot(0, combo, [combo]() { combo->showPopup(); });
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) return QStyledItemDelegate::setEditorData(editor, index);
        const QString cur = index.data(Qt::EditRole).toString().trimmed();
        int i = combo->findData(cur, Qt::UserRole, Qt::MatchFixedString);
        if (i < 0 && !cur.isEmpty()) {   // keep a code that isn't in the ECO file visible, not silently replaced
            combo->insertItem(0, cur + "  (not in ECO file)", cur);
            i = 0;
        }
        combo->setCurrentIndex(qMax(i, 0));
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) return QStyledItemDelegate::setModelData(editor, model, index);
        const QString picked = combo->currentData().toString();
        if (picked != index.data(Qt::EditRole).toString().trimmed())
            model->setData(index, picked);
    }

private:
    std::function<EcoList()> m_ecotypes;
};
}

// ─── Proxy: pins MINIMA/MAXIMA rows to the top during any sort ───────────────
class CulSortProxy : public QSortFilterProxyModel {
public:
    explicit CulSortProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    // Row numbers in display order (the source row would show file positions)
    QVariant headerData(int section, Qt::Orientation o, int role) const override {
        if (o == Qt::Vertical && role == Qt::DisplayRole) return section + 1;
        return QSortFilterProxyModel::headerData(section, o, role);
    }

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

    // Keep a row the user just added/duplicated visible while the
    // "used in experiments" filter is on (its new VAR# isn't in any experiment).
    void keepVisible(const QString &varNum) {
        if (!m_filterActive) return;
        m_usedVarNums.insert(varNum.trimmed());
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
        if (lPin && rPin)   // MINIMA (999991) above MAXIMA (999992) in either direction
            return (sortOrder() == Qt::AscendingOrder) ? lv < rv : lv > rv;
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

    // Row numbers in display order (the source row would show file positions)
    QVariant headerData(int section, Qt::Orientation o, int role) const override {
        if (o == Qt::Vertical && role == Qt::DisplayRole) return section + 1;
        return QSortFilterProxyModel::headerData(section, o, role);
    }
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        // ECO# is always column 0 in the source model
        const QString lv = sourceModel()->data(sourceModel()->index(left.row(),  0)).toString();
        const QString rv = sourceModel()->data(sourceModel()->index(right.row(), 0)).toString();
        const bool lPin = (lv == "999991" || lv == "999992");
        const bool rPin = (rv == "999991" || rv == "999992");
        if (lPin && rPin)   // MINIMA (999991) above MAXIMA (999992) in either direction
            return (sortOrder() == Qt::AscendingOrder) ? lv < rv : lv > rv;
        if (lPin != rPin) {
            // Keep pinned rows (MINIMA/MAXIMA) above all regular rows regardless of sort direction
            return (sortOrder() == Qt::AscendingOrder) ? lPin : !lPin;
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

// ─── constructor ────────────────────────────────────────────────────────────
// Row the user means: the current cell, else the first selected cell (row
// selection from the header, screen readers and UI Automation leave no current cell).
static QModelIndex activeIndex(const QAbstractItemView *view)
{
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid() && view->selectionModel() && view->selectionModel()->hasSelection())
        idx = view->selectionModel()->selectedIndexes().first();
    return idx;
}

// One-line colour key under a table: swatches drawn with the colours the models use
static QLabel *makeLegend(const QList<QPair<QColor, QString>> &swatches, const QString &extra = QString())
{
    QStringList parts;
    for (const auto &sw : swatches)
        parts << QString("<span style='background:%1; border:1px solid #B0B8C0;'>&nbsp;&nbsp;&nbsp;&nbsp;</span>&nbsp;%2")
                     .arg(sw.first.name(), sw.second.toHtmlEscaped());
    if (!extra.isEmpty()) parts << extra;
    auto *l = new QLabel(parts.join("&nbsp;&nbsp;&nbsp;&nbsp;"));
    l->setTextFormat(Qt::RichText);
    l->setStyleSheet("color: #444; font-size: 11px; padding: 2px 2px;");
    return l;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_culModel(new CulTableModel(this))
    , m_ecoModel(new EcoTableModel(this))
{
    setWindowTitle("DSSAT Genetics Editor");   // crop and file added on load; version is in Help > About
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
        "QTabBar::tab:hover:!selected { background-color: #EEF3F7; }"
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
        // Buttons are plain (blue text, thin border); only the primary action
        // (Config::ACCENT_BUTTON_STYLE) is filled, and Delete fills red on hover
        "QPushButton { background-color: white; color: #1F6FB5; border: 1px solid #1F6FB5;"
        "  border-radius: 4px; padding: 5px 12px; font-weight: bold; min-width: 60px; }"
        "QPushButton:hover { background-color: #E8F1FA; }"
        "QPushButton:pressed { background-color: #D0E3F5; }"
        "QPushButton:disabled { background-color: #F0F3F6; color: #9AA5B1; border-color: #C9D6DF; }"
        "QPushButton#dangerBtn { color: #C62828; border-color: #C62828; }"
        "QPushButton#dangerBtn:hover { background-color: #C62828; color: white; }"
        "QPushButton#dangerBtn:pressed { background-color: #A31515; color: white; }"
        // #id rules outrank :disabled, so restate it for the danger button
        "QPushButton#dangerBtn:disabled { background-color: #F0F3F6; color: #9AA5B1; border-color: #C9D6DF; }"
        "QTextEdit { background-color: white; border: 1px solid #C9D6DF; }"
        "QListWidget { background-color: white; border: 1px solid #C9D6DF; }"
        "QSplitter::handle { background-color: #C9D6DF; }"
        "QStatusBar { background-color: #E4E8ED; }"
        "QToolTip { background-color: #FFFFFF; color: #1F2933; border: 1px solid #C9D6DF;"
        "  padding: 4px 6px; }"
    );

    setupUI();
    connectSignals();

    // Centre window
    if (QScreen *screen = QApplication::primaryScreen()) {
        QRect sg = screen->availableGeometry();
        move((sg.width() - width()) / 2, (sg.height() - height()) / 2);
    }

    // Resolve GLUE paths (QSettings → common locations → DSSATPRO DGL entry)
    // before the first crop loads: Run GLUE's state reads GlueExcludedModels.csv
    GlueRunner::resolvePaths(Config::DSSATPRO_FILE);

    // Auto-load DSSAT config
    if (QFile::exists(Config::DSSATPRO_FILE))
        loadDssatConfig(Config::DSSAT_BASE);
}

MainWindow::~MainWindow() {}

// ─── close event ────────────────────────────────────────────────────────────
void MainWindow::closeEvent(QCloseEvent *event)
{
    // GLUE's R workers and DSSAT runs would otherwise keep going after exit
    if (m_glueQueue && m_glueQueue->isRunning()) {
        QMessageBox box(QMessageBox::Question, "GLUE is running",
                        "A GLUE calibration is still running.\n\nStop it and close?",
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        // The run may finish while this is open: then there is nothing to stop, so close
        auto finished = connect(m_glueQueue, &GlueQueueManager::queueChanged, &box, [&box, this]() {
            if (!m_glueQueue->isRunning()) box.done(QMessageBox::Yes);
        });
        const int answer = box.exec();
        disconnect(finished);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        if (m_glueQueue->isRunning()) m_glueQueue->stop();
    }

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
    // One row (DSSAT folder | crop); the file name is shown in the window title
    QGroupBox *topGroup = new QGroupBox(central);
    QGridLayout *topGrid = new QGridLayout(topGroup);
    topGrid->setHorizontalSpacing(8);
    topGrid->setContentsMargins(8, 6, 8, 6);

    topGrid->addWidget(new QLabel("DSSAT folder:"), 0, 0);
    m_dssatDirEdit = new QLineEdit(Config::DSSAT_BASE);
    m_dssatDirEdit->setToolTip(m_dssatDirEdit->text());
    connect(m_dssatDirEdit, &QLineEdit::textChanged, m_dssatDirEdit, &QLineEdit::setToolTip);
    topGrid->addWidget(m_dssatDirEdit, 0, 1);
    m_browseButton = new QPushButton("Browse…");
    m_browseButton->setFixedWidth(80);
    topGrid->addWidget(m_browseButton, 0, 2);

    topGrid->addItem(new QSpacerItem(16, 1), 0, 3);
    topGrid->addWidget(new QLabel("Crop:"), 0, 4);
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
    topGrid->addWidget(m_cropCombo, 0, 5);
    topGrid->setColumnStretch(1, 1);
    topGrid->setColumnStretch(5, 1);

    // Current file: not shown here any more, but kept for the window title
    m_geneticsLabel = new QLabel("—", topGroup);
    m_geneticsLabel->setVisible(false);

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
    fileMenu->addAction("Set R directory…",      this, &MainWindow::onOpenRDir);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", qApp, &QApplication::quit);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainWindow::onAbout);
}

void MainWindow::setupCropBar() {}  // logic merged into setupUI

// Replaces the Save buttons: edits save automatically, this shows whether they have
static QLabel *makeSaveState()
{
    auto *l = new QLabel("\u2713 Saved");
    l->setMinimumWidth(90);
    l->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    l->setStyleSheet("color: #2E7D32;");
    l->setToolTip(Config::wrappedTip("Changes are saved automatically about a second after each edit;"
                                     " the previous file is kept as a timestamped .bak next to it."
                                     "\nCtrl+S saves immediately."));
    return l;
}

static QPushButton *makeBtn(const QString &text, const QString &objName = {})
{
    auto *btn = new QPushButton(text);
    if (!objName.isEmpty()) btn->setObjectName(objName);
    return btn;
}

// The stylesheet's min-width becomes the button's minimum size, which lets a
// crowded toolbar squeeze a button narrower than its label. Pin the minimum to
// the bold label plus the stylesheet's 12px side padding and 1px border, and
// re-pin it whenever the stylesheet is re-applied (that resets the minimum).
static void applyFullLabelWidth(QPushButton *btn)
{
    QFont bold = btn->font();
    bold.setBold(true);
    const int labelW = QFontMetrics(bold).horizontalAdvance(btn->text()) + 2 * 12 + 2 + 6;
    btn->setMinimumWidth(qMax(btn->sizeHint().width(), labelW));
}

namespace {
class FullLabelFilter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() == QEvent::Polish || ev->type() == QEvent::StyleChange)
            applyFullLabelWidth(static_cast<QPushButton *>(obj));
        return false;
    }
};
}

static void keepFullLabel(QPushButton *btn)
{
    if (!btn->property("fullLabel").toBool()) {
        btn->setProperty("fullLabel", true);
        btn->installEventFilter(new FullLabelFilter(btn));
    }
    applyFullLabelWidth(btn);
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
    // Toolbar is crowded: the search box gives way first so buttons keep their full labels
    m_culSearch->setMinimumWidth(90);
    toolbar->addWidget(m_culSearch);
    toolbar->addStretch();

    m_culAddBtn      = makeBtn("Add");
    m_culDelBtn      = makeBtn("Delete",    "dangerBtn");
    m_culDupBtn      = makeBtn("Duplicate");
    m_culSaveState   = makeSaveState();
    m_culRefreshBtn = makeBtn("Refresh");
    m_culShowUsedBtn = new QCheckBox("Used in experiments only");
    m_culShowUsedBtn->setToolTip("Show only cultivars listed in this crop's experiment files");
    m_culCountLabel = new QLabel;
    m_culCountLabel->setStyleSheet("color: #555;");
    QPushButton *culGlueBtn = makeBtn("Paste GLUE");
    culGlueBtn->setToolTip("Paste a GLUE-calibrated cultivar line to update or add a row");
    connect(culGlueBtn, &QPushButton::clicked, this, &MainWindow::onCulPasteGlue);

    m_culGlueQueueBtn = makeBtn("Run GLUE");
    QPushButton *addQueueBtn = m_culGlueQueueBtn;
    addQueueBtn->setToolTip("Select treatments and add this cultivar to the GLUE calibration queue");
    addQueueBtn->setStyleSheet(Config::ACCENT_BUTTON_STYLE);
    connect(addQueueBtn, &QPushButton::clicked, this, [this]() {
        QModelIndex idx = activeIndex(m_culView);
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
        const QString unsupported = glueUnsupportedReason(m_currentCropCode);
        if (!unsupported.isEmpty()) {
            QMessageBox::warning(this, "GLUE Queue", unsupported);
            return;
        }
        GlueQueueDialog dlg(m_crops[m_currentCropCode], varNum, vrName, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_glueQueue->addEntry(dlg.result());
            m_culGlueQueueBtn->setText("Queue GLUE");   // short: a 1200px window has no spare toolbar width
            keepFullLabel(m_culGlueQueueBtn);
        }
    });

    toolbar->insertWidget(toolbar->count() - 1, m_culShowUsedBtn);   // before the stretch
    toolbar->insertWidget(toolbar->count() - 1, m_culCountLabel);
    // Groups: rows | file | GLUE | save state
    for (QPushButton *b : {m_culAddBtn, m_culDupBtn, m_culDelBtn})
        toolbar->addWidget(b);
    toolbar->addSpacing(8);
    toolbar->addWidget(m_culRefreshBtn);
    toolbar->addSpacing(8);
    toolbar->addWidget(culGlueBtn);
    toolbar->addWidget(addQueueBtn);
    toolbar->addSpacing(8);
    toolbar->addWidget(m_culSaveState);

    vbox->addLayout(toolbar);
    for (QPushButton *b : {m_culAddBtn, m_culDupBtn, m_culDelBtn, m_culRefreshBtn, culGlueBtn, addQueueBtn})
        keepFullLabel(b);

    // Table view with sort/filter proxy
    m_culProxy = new CulSortProxy(this);
    m_culProxy->setSourceModel(m_culModel);
    m_culProxy->setSortRole(CulTableModel::SortRole);
    m_culProxy->setFilterKeyColumn(-1);
    m_culProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    // VAR#, VRNAME, EXPNO, ECO# stay while parameters scroll; P/G/N band under the names
    auto *culView = new FrozenTableView(4, true);
    m_culView = culView;
    connect(culView, &FrozenTableView::calibrationClicked, this, &MainWindow::showCalibrationMenu);
    m_culView->setModel(m_culProxy);
    culView->setColumnDelegate(CulTableModel::COL_ECONUM, new EcoComboDelegate([this]() {
        EcoComboDelegate::EcoList list;
        for (const EcoRow &r : m_ecoModel->rows())
            if (!r.isMinMax && !r.ecoNum.trimmed().isEmpty())
                list.append({r.ecoNum.trimmed(), r.ecoName.trimmed()});
        return list;
    }, this));
    m_culView->setAlternatingRowColors(true);
    culView->enableUserColumnWidths("CUL");
    m_culView->horizontalHeader()->setStretchLastSection(false);
    m_culView->verticalHeader()->setDefaultSectionSize(22);
    m_culView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_culView->setSortingEnabled(true);
    m_culView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);   // file order, new rows at the end

    // Right-click on column headers to change P/G/N calibration type
    m_culView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_culView->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &MainWindow::onCulHeaderContextMenu);

    // Queue panel below table in a splitter
    m_gluePanel = new GlueQueuePanel(m_glueQueue, tab);
    m_gluePanel->setMaximumHeight(160);

    QSplitter *splitter = new QSplitter(Qt::Vertical, tab);
    splitter->addWidget(m_culView);
    splitter->addWidget(m_gluePanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    vbox->addWidget(splitter, 1);
    vbox->addWidget(makeLegend({{Config::MINMAX_COLOR, "MINIMA / MAXIMA"},
                                {Config::OOR_COLOR,    "Outside MINIMA–MAXIMA"}},
        "<b style='color:#1565C0'>[P]</b> phenology &nbsp;<b style='color:#2E7D32'>[G]</b> growth"
        " &nbsp;<b style='color:#9E9E9E'>[N]</b> not calibrated &nbsp;(click P/G/N under a column name to change)"));

    // Queue panel only takes room once something is queued
    m_gluePanel->setVisible(false);
    connect(m_glueQueue, &GlueQueueManager::queueChanged, this, [this]() {
        m_gluePanel->setVisible(!m_glueQueue->entries().isEmpty());
    });

    // "13 of 116 cultivars" next to the filter checkbox
    // The proxy reports a removal before the source row is gone, so also recount
    // once the source model has actually changed.
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(m_culProxy, sig, this, &MainWindow::updateCulCount);
        connect(m_culModel, sig, this, &MainWindow::updateCulCount);
    }
    connect(m_culProxy, &QAbstractItemModel::modelReset,    this, &MainWindow::updateCulCount);
    connect(m_culProxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateCulCount);

    // Refused edits revert silently in the table, so say why — after the editor closes
    connect(m_culModel, &CulTableModel::valueRejected, this, [this](const QString &reason) {
        setStatus(reason, true);
        QTimer::singleShot(0, this, [this, reason]() {
            QMessageBox::warning(this, "Value not allowed", reason);
        });
    });

    // Auto-apply calibrated results to CUL model
    connect(m_glueQueue, &GlueQueueManager::entryFinished,
            this, [this](int index, bool success, const QString &culLine) {
        if (!success || culLine.isEmpty()) {
            const GlueQueueEntry &e = m_glueQueue->entries().at(index);
            if (e.status == GlueQueueStatus::Stopped)
                setStatus(QString("GLUE stopped for %1").arg(e.cultivarId));
            else
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

        // Inline history comment: stamp the old line with date before overwriting
        {
            int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
            QVector<ParamFormat> fmts = CulParser::inferFormats(m_culModel->rows(), numParams);
            QString oldLine = CulParser::formatRow(m_culModel->rows().at(culRow), fmts, numParams);
            QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
            // Append below any existing comment (earlier history, author notes) — don't replace it
            const QString prev = m_culModel->rows().at(culRow).preComment;
            const QString hist = "! " + ts + " " + oldLine.trimmed();
            m_culModel->setRowPreComment(culRow, prev.isEmpty() ? hist : prev + "\n" + hist);
        }

        // Parse and apply params — fixed-width: VARNUM(6) SP VRNAME(16) EXPNO(7) ECO(6) SP params
        // Params start at position 36 (a value may fill its whole field)
        QString paramStr = culLine.mid(36).trimmed();
        QStringList vals = paramStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        int nParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
        for (int i = 0; i < qMin(vals.size(), nParams); ++i) {
            bool ok = false;
            vals[i].toDouble(&ok);
            if (ok)   // pass GLUE's text, so its precision ("48.00") is kept on save
                m_culModel->setData(m_culModel->index(culRow, CulTableModel::COL_PARAM0 + i), vals[i]);
        }

        // Auto-save directly — no .bak file, inline comment already written above
        if (!m_currentCulPath.isEmpty()) {
            QStringList pNames;
            int n = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
            for (int i = 0; i < n; ++i)
                pNames << m_culModel->columnName(CulTableModel::COL_PARAM0 + i);
            CulParser::write(m_currentCulPath, m_culModel->rows(), m_culHeaderLines, pNames, m_culFooterLines);
            m_culDirty = false;
            setStatus(QString("GLUE calibration applied and saved for %1").arg(varNum));
        } else {
            setStatus(QString("GLUE calibration applied to %1 — click Save to write to file.").arg(varNum));
        }
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
    m_ecoSaveState = makeSaveState();

    for (QPushButton *b : {m_ecoAddBtn, m_ecoDupBtn, m_ecoDelBtn})
        toolbar->addWidget(b);
    toolbar->addSpacing(16);
    toolbar->addWidget(m_ecoSaveState);

    vbox->addLayout(toolbar);

    m_ecoProxy = new EcoSortProxy(this);
    m_ecoProxy->setSourceModel(m_ecoModel);
    m_ecoProxy->setSortRole(EcoTableModel::SortRole);
    m_ecoProxy->setFilterKeyColumn(-1);
    m_ecoProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_ecoView = new FrozenTableView(2);   // ECO#, ECONAME
    m_ecoView->setModel(m_ecoProxy);
    m_ecoView->setAlternatingRowColors(true);
    static_cast<FrozenTableView *>(m_ecoView)->enableUserColumnWidths("ECO");
    m_ecoView->verticalHeader()->setDefaultSectionSize(22);
    m_ecoView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ecoView->setSortingEnabled(true);
    m_ecoView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);   // file order, new rows at the end
    vbox->addWidget(m_ecoView, 1);
    vbox->addWidget(makeLegend({{Config::MINMAX_COLOR,  "MINIMA / MAXIMA"}}));

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
    m_speSaveState = makeSaveState();
    toolbar->addWidget(m_speSaveState);
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
    editSplitter->setStretchFactor(1, 2);
    // Start with ~40% for the graph so its axis labels fit; a minimum height
    // here would raise every tab's minimum and push the status bar out of view
    editSplitter->setSizes({300, 220});
    
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
    connect(m_culRefreshBtn,  &QPushButton::clicked, this, &MainWindow::onCulRefresh);
    connect(m_culShowUsedBtn, &QCheckBox::toggled, this, &MainWindow::onCulShowUsed);
    connect(m_culSearch,      &QLineEdit::textChanged, this, &MainWindow::onCulSearch);
    connect(m_culModel,       &CulTableModel::dataModified,
            [this](){ m_culDirty = true; setStatus("CUL modified…"); m_autoSaveTimer->start(); });

    // ECO
    connect(m_ecoAddBtn,  &QPushButton::clicked, this, &MainWindow::onEcoAdd);
    connect(m_ecoDelBtn,  &QPushButton::clicked, this, &MainWindow::onEcoDelete);
    connect(m_ecoDupBtn,  &QPushButton::clicked, this, &MainWindow::onEcoDuplicate);
    connect(m_ecoSearch,  &QLineEdit::textChanged, this, &MainWindow::onEcoSearch);
    connect(m_ecoModel,   &EcoTableModel::dataModified,
            [this](){ m_ecoDirty = true; setStatus("ECO modified…"); m_autoSaveTimer->start(); });

    // SPE

    // Saved-state labels follow the dirty / failed flags; Ctrl+S saves right away
    auto *saveStateTimer = new QTimer(this);
    connect(saveStateTimer, &QTimer::timeout, this, &MainWindow::updateSaveIndicators);
    saveStateTimer->start(300);
    auto *saveNow = new QShortcut(QKeySequence::Save, this);
    connect(saveNow, &QShortcut::activated, this, [this]() {
        if (m_autoSaveTimer) m_autoSaveTimer->stop();
        autoSaveAll();
        updateSaveIndicators();
    });
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

QString MainWindow::glueUnsupportedReason(const QString &cropKey) const
{
    if (!m_crops.contains(cropKey)) return QString();
    const CropInfo &info = m_crops[cropKey];
    QString reason = GlueRunner::unsupportedReason(info);
    if (reason.isEmpty()) return reason;

    // Point to another model of the same crop only if GLUE supports one
    QStringList alternatives;
    for (auto it = m_crops.cbegin(); it != m_crops.cend(); ++it) {
        if (it.key() == cropKey) continue;
        if (it->cropCode.compare(info.cropCode, Qt::CaseInsensitive) != 0) continue;
        if (GlueRunner::unsupportedReason(*it).isEmpty()) alternatives << it->module;
    }
    if (alternatives.isEmpty())
        reason += "\n\nNo GLUE-supported model is available for this crop, so its "
                  "cultivars cannot be calibrated with GLUE.";
    else
        reason += QString("\n\nSelect %1 in the Crop list to run GLUE for this crop.")
                      .arg(alternatives.join(" or "));
    return reason;
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
    if (m_culNewRowFade) m_culNewRowFade->stop();
    // Remembered column widths are per crop
    static_cast<FrozenTableView *>(m_culView)->setColumnWidthScope(cropCode);
    static_cast<FrozenTableView *>(m_ecoView)->setColumnWidthScope(cropCode);
    const CropInfo &info = m_crops[cropCode];
    m_currentCropCode = cropCode;
    m_currentCulPath  = info.culFile;
    m_currentEcoPath  = info.ecoFile;
    m_currentSpePath  = info.speFile;

    m_geneticsLabel->setText("File: —");

    // Reset "Show Used" toggle when crop changes
    m_culShowUsedBtn->blockSignals(true);
    m_culShowUsedBtn->setChecked(false);
    m_culShowUsedBtn->blockSignals(false);
    m_culProxy->clearUsedFilter();
    // Disable GLUE up front for models GLUE.r refuses, instead of after a click
    const QString glueBlock = glueUnsupportedReason(cropCode);
    if (m_culGlueQueueBtn) {
        m_culGlueQueueBtn->setText("Run GLUE");
        keepFullLabel(m_culGlueQueueBtn);
        m_culGlueQueueBtn->setEnabled(glueBlock.isEmpty());
        m_culGlueQueueBtn->setToolTip(glueBlock.isEmpty()
            ? "Select treatments and add this cultivar to the GLUE calibration queue"
            : glueBlock);
    }

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
        QVector<CulRow> culRows = CulParser::parse(m_currentCulPath, m_culHeaderLines, &m_culFooterLines);
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
            {
                EcoLayout lay;
                QVector<EcoRow> er = EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines, &m_ecoFooterLines, &lay);
                m_ecoModel->setRows(er, lay);
            }
            m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
            m_ecoDirty = false;
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
                m_culShowUsedBtn->blockSignals(false);
                updateCulCount();
                setStatus(QString("Loaded CUL: %1 — %2 used in experiments.")
                              .arg(QFileInfo(m_currentCulPath).fileName(), m_culCountLabel->text()));
            }
        }
    } else if (fileType == "ECO") {
        m_ecoHeaderLines.clear();
        EcoLayout ecoLayout;
        QVector<EcoRow> ecoRows = EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines, &m_ecoFooterLines, &ecoLayout);
        m_ecoModel->setRows(ecoRows, ecoLayout);
        m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
        m_ecoDirty = false;
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
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString title = "DSSAT Genetics Editor";
    if (m_cropCombo && m_cropCombo->currentIndex() >= 0) {
        title += " — " + m_cropCombo->currentText();
        const QString file = m_geneticsLabel ? m_geneticsLabel->text().section(": ", 1) : QString();
        if (!file.isEmpty() && file != "—") title += " · " + file;
    }
    setWindowTitle(title);
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

    // Ask for a text field until it fits its fixed width (or Cancel)
    auto askField = [&](const QString &label, const QString &def, int maxLen,
                        bool allowSpaces, bool allowEmpty, QString &out) {
        for (;;) {
            out = QInputDialog::getText(this, "New Cultivar",
                QString("%1 (max %2 chars):").arg(label).arg(maxLen),
                QLineEdit::Normal, def, &ok).trimmed();
            if (!ok) return false;
            if ((allowEmpty || !out.isEmpty()) && out.size() <= maxLen
                && (allowSpaces || !out.contains(' ')))
                return true;
            QMessageBox::warning(this, "New Cultivar",
                QString("%1 must be %2at most %3 characters%4.")
                    .arg(label, allowEmpty ? "" : "non-empty and ")
                    .arg(maxLen).arg(allowSpaces ? "" : " without spaces"));
        }
    };

    QString vrName, expNo, ecoNum;
    if (!askField("Cultivar name (VRNAME)", "NEW CULTIVAR", 16, true, false, vrName)) return;
    if (!askField("Experiment numbers (EXPNO)", ".", 6, false, true, expNo)) return;
    if (!askField("Ecotype code (ECO#)", "DFAULT", 6, false, false, ecoNum)) return;
    if (expNo.isEmpty()) expNo = ".";

    // Ask for each parameter. Values must be numbers that fit the column.
    // Skip leaves one value missing, Skip rest all remaining ones; DSSAT's
    // missing value -99 is written for them.
    int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
    QVector<std::optional<double>> params(numParams);
    const QVector<ParamFormat> fmts = CulParser::inferFormats(m_culModel->rows(), numParams);

    for (int i = 0; i < numParams; ++i) {
        const QString paramName = m_culModel->columnName(CulTableModel::COL_PARAM0 + i);
        const int maxLen = i < fmts.size() ? fmts[i].width : 5;

        QDialog dialog(this);
        dialog.setWindowTitle("New Cultivar Parameters");
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        double lo = 0, hi = 0;
        const bool ranged = m_culModel->allowedRange(i, &lo, &hi);
        layout->addWidget(new QLabel(ranged
            ? QString("Enter %1 (number, max %2 chars, from %3 to %4):")
                  .arg(paramName).arg(maxLen).arg(lo, 0, 'g', 6).arg(hi, 0, 'g', 6)
            : QString("Enter %1 (number, max %2 chars):").arg(paramName).arg(maxLen)));
        QLineEdit *lineEdit = new QLineEdit();
        layout->addWidget(lineEdit);
        QLabel *errLabel = new QLabel;
        errLabel->setStyleSheet("color: #c0392b;");
        errLabel->setWordWrap(true);
        layout->addWidget(errLabel);

        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *addBtn     = new QPushButton("Add");
        QPushButton *skipBtn    = new QPushButton("Skip");
        QPushButton *skipAllBtn = new QPushButton("Skip rest");
        addBtn->setDefault(true);
        buttonLayout->addWidget(addBtn);
        buttonLayout->addWidget(skipBtn);
        buttonLayout->addWidget(skipAllBtn);
        layout->addLayout(buttonLayout);

        bool skipRest = false;
        connect(addBtn, &QPushButton::clicked, [&]() {
            const QString t = lineEdit->text().trimmed();
            bool num;
            const double v = t.toDouble(&num);
            if (!num || t.size() > maxLen) {
                errLabel->setText(QString("Enter a number of at most %1 characters.").arg(maxLen));
                return;
            }
            const QString rangeErr = m_culModel->rangeError(i, v);
            if (!rangeErr.isEmpty()) {
                errLabel->setText(rangeErr);
                dialog.adjustSize();   // grow to show the wrapped message, not clip it
                return;
            }
            params[i] = v;
            dialog.accept();
        });
        connect(skipBtn,    &QPushButton::clicked, [&]() { dialog.reject(); });
        connect(skipAllBtn, &QPushButton::clicked, [&]() { skipRest = true; dialog.reject(); });

        dialog.exec();   // rejected (Skip / Esc) leaves this value missing
        if (skipRest) break;
    }

    // Create row with all user-provided values
    int newRow = m_culModel->rowCount();
    m_culModel->addRowWithFullData(vrName, expNo, ecoNum, params);
    m_culDirty = true;

    // Focus on the new row
    selectNewCulRow(newRow);
}

void MainWindow::selectNewCulRow(int sourceRow)
{
    QModelIndex srcIdx = m_culModel->index(sourceRow, CulTableModel::COL_VARNUM);
    m_culProxy->keepVisible(srcIdx.data().toString());
    QModelIndex newIdx = m_culProxy->mapFromSource(srcIdx);
    if (!newIdx.isValid()) return;

    // Make the new row current without selecting it, so the green tint isn't
    // hidden under the selection colour; buttons act on the current row anyway.
    m_culView->selectionModel()->setCurrentIndex(newIdx, QItemSelectionModel::Clear);
    m_culView->scrollTo(newIdx);

    const QString varNum = srcIdx.data().toString();
    if (!m_culNewRowFade) {
        m_culNewRowFade = new QVariantAnimation(this);
        m_culNewRowFade->setStartValue(1.0);
        m_culNewRowFade->setEndValue(0.0);
        m_culNewRowFade->setDuration(3000);
        m_culNewRowFade->setEasingCurve(QEasingCurve::InCubic);   // stays strong, then fades
    }
    m_culNewRowFade->stop();
    m_culNewRowFade->disconnect(this);
    connect(m_culNewRowFade, &QVariantAnimation::valueChanged, this, [this, varNum](const QVariant &v) {
        m_culModel->setHighlight(varNum, v.toReal());
    });
    connect(m_culNewRowFade, &QVariantAnimation::finished, this, [this, varNum]() {
        m_culModel->setHighlight({}, 0);
        // Hand over to the normal selection, unless the user has moved on
        QModelIndex cur = m_culProxy->mapToSource(m_culView->currentIndex());
        if (cur.isValid() && !m_culView->selectionModel()->hasSelection()
            && m_culModel->index(cur.row(), CulTableModel::COL_VARNUM).data().toString() == varNum)
            m_culView->selectRow(m_culView->currentIndex().row());
    });
    m_culModel->setHighlight(varNum, 1.0);
    m_culNewRowFade->start();
}

void MainWindow::onCulDelete()
{
    QModelIndex idx = m_culProxy->mapToSource(activeIndex(m_culView));
    if (!idx.isValid()) return;

    auto btn = QMessageBox::question(this, "Delete cultivar",
        "Delete selected cultivar row?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn == QMessageBox::Yes)
        m_culModel->deleteRow(idx.row());
}

void MainWindow::onCulDuplicate()
{
    QModelIndex idx = m_culProxy->mapToSource(activeIndex(m_culView));
    if (!idx.isValid()) {
        setStatus("Select a cultivar row to duplicate.", true);
        return;
    }
    const QString srcVar = m_culModel->index(idx.row(), CulTableModel::COL_VARNUM).data().toString();
    int newRow = m_culModel->rowCount();
    m_culModel->duplicateRow(idx.row());
    if (m_culModel->rowCount() == newRow) return;
    selectNewCulRow(newRow);
    setStatus(QString("Duplicated %1 as %2.")
                  .arg(srcVar.trimmed(),
                       m_culModel->index(newRow, CulTableModel::COL_VARNUM).data().toString().trimmed()));
}

void MainWindow::onCulSave()
{
    if (m_currentCulPath.isEmpty()) return;

    // Backup
    BackupManager::createBackup(m_currentCulPath);

    QStringList paramNames;
    int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
    for (int i = 0; i < numParams; ++i) {
        paramNames << m_culModel->columnName(CulTableModel::COL_PARAM0 + i);
    }

    if (CulParser::write(m_currentCulPath, m_culModel->rows(), m_culHeaderLines, paramNames, m_culFooterLines)) {
        m_culDirty = false;
        const int oor = m_culModel->getViolations().size();
        if (oor > 0)
            setStatus(QString("CUL saved: %1 — %2 value(s) outside the MINIMA/MAXIMA range"
                              " (highlighted; Refresh lists them)").arg(m_currentCulPath).arg(oor), true);
        else
            setStatus("CUL saved: " + m_currentCulPath);
        m_culSaveFailed = false;
    } else {
        m_culSaveFailed = true;
        setStatus("Failed to save: " + m_currentCulPath, true);
    }
}

void MainWindow::onCulCopyRow()
{
    QModelIndex idx = m_culProxy->mapToSource(activeIndex(m_culView));
    if (!idx.isValid()) return;
    const CulRow &row = m_culModel->rows().at(idx.row());
    int numParams = m_culModel->columnCount() - CulTableModel::COL_PARAM0;
    QVector<ParamFormat> fmts = CulParser::inferFormats(m_culModel->rows(), numParams);
    QApplication::clipboard()->setText(CulParser::formatRow(row, fmts, numParams));
    setStatus(QString("Copied cultivar %1 to clipboard").arg(row.varNum));
}

void MainWindow::onEcoCopyRow()
{
    QModelIndex idx = m_ecoProxy->mapToSource(activeIndex(m_ecoView));
    if (!idx.isValid()) return;
    const EcoRow &row = m_ecoModel->rows().at(idx.row());
    QApplication::clipboard()->setText(EcoParser::formatRow(row, m_ecoModel->layout()));
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
            // pass the pasted text, so GLUE precision ("3.500") is kept on save
            QVariant val = !newRow.params[p].has_value() ? QVariant()
                         : p < newRow.paramStrs.size() ? QVariant(newRow.paramStrs[p])
                                                       : QVariant(newRow.params[p].value());
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
            // pass the pasted text, so GLUE precision ("3.500") is kept on save
            QVariant val = !newRow.params[p].has_value() ? QVariant()
                         : p < newRow.paramStrs.size() ? QVariant(newRow.paramStrs[p])
                                                       : QVariant(newRow.params[p].value());
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
        QVector<CulRow> culRows = CulParser::parse(m_currentCulPath, m_culHeaderLines, &m_culFooterLines);
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
        {
                EcoLayout lay;
                QVector<EcoRow> er = EcoParser::parse(m_currentEcoPath, m_ecoHeaderLines, &m_ecoFooterLines, &lay);
                m_ecoModel->setRows(er, lay);
            }
        m_ecoModel->setColumnTooltips(CulParser::tooltipsFromHeader(m_ecoHeaderLines));
        m_ecoDirty = false;
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
                    issues << row.varNum + ": param "
                              + m_culModel->columnName(CulTableModel::COL_PARAM0 + i) + " is not finite";
            }
        }
    }

    for (const auto &v : m_culModel->getViolations())
        issues << "CUL " + v.toString();

    for (const auto &cr : m_culModel->rows())
        if (cr.misaligned)
            issues << "CUL " + cr.varNum + ": row is not in DSSAT fixed-width format in the file"
                      " (values off their columns, a blank field, or a field longer than its width)"
                      " — check it; saving rewrites it in fixed-width format";

    for (const auto &er : m_ecoModel->rows())
        if (er.misaligned)
            issues << "ECO " + er.ecoNum + ": row is not in DSSAT fixed-width format in the file"
                      " (values off their columns or name longer than 16 chars) — check it;"
                      " saving rewrites it in fixed-width format";

    if (issues.isEmpty()) {
        QMessageBox::information(this, "Refresh", "Files reloaded — no issues found.");
    } else {
        QString msg = QString("%1 issue(s) found:\n\n").arg(issues.size());
        msg += issues.mid(0, 50).join("\n");
        if (issues.size() > 50) msg += "\n… and more";
        QMessageBox::warning(this, "Refresh — Issues Found", msg);
    }
}

void MainWindow::updateCulCount()
{
    auto regular = [](const QAbstractItemModel *m) {
        int n = 0;
        for (int r = 0; r < m->rowCount(); ++r) {
            const QString v = m->index(r, CulTableModel::COL_VARNUM).data().toString();
            if (v != "999991" && v != "999992") ++n;
        }
        return n;
    };
    const int total = regular(m_culModel), shown = regular(m_culProxy);
    m_culCountLabel->setText(shown == total ? QString("%1 cultivars").arg(total)
                                            : QString("%1 of %2 cultivars").arg(shown).arg(total));
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
    updateCulCount();
    setStatus(QString("Showing %1 used in experiments.").arg(m_culCountLabel->text()));
}

// ─── ECO actions ─────────────────────────────────────────────────────────────
void MainWindow::onEcoAdd()  
{ 
    bool ok;
    const EcoLayout &lay = m_ecoModel->layout();
    if (lay.names.isEmpty()) {
        setStatus("ECO file has no @ECO# column header — cannot add an ecotype.", true);
        return;
    }

    // Ask for ECONAME (only if this model's ECO file has the column)
    QString ecoName;
    if (lay.hasName) {
        ecoName = QInputDialog::getText(this, "New Ecotype",
            "Enter ecotype name (ECONAME, max 16 chars):", QLineEdit::Normal,
            "NEW ECOTYPE", &ok);
        if (!ok || ecoName.trimmed().isEmpty()) return;
    }

    // Ask for each column in file order. Values must fit the column; Cancel stops,
    // and the remaining columns are written as missing (-99, or "." for text columns).
    QStringList vals;
    for (int i = 0; i < lay.names.size(); ++i) {
        const int maxLen = lay.width(i) - 1;
        QString v;
        for (;;) {
            v = QInputDialog::getText(this, "New Ecotype",
                QString("Enter %1 (%2, max %3 chars) — Cancel to leave the rest missing:")
                    .arg(lay.names[i], lay.numeric[i] ? "number" : "text").arg(maxLen),
                QLineEdit::Normal, lay.numeric[i] ? "-99" : ".", &ok).trimmed();
            if (!ok) break;
            bool num = true;
            if (lay.numeric[i]) v.toDouble(&num);
            if (num && !v.isEmpty() && !v.contains(' ') && v.size() <= maxLen) break;
            QMessageBox::warning(this, "New Ecotype",
                QString("%1 must be a single %2 of at most %3 characters.")
                    .arg(lay.names[i], lay.numeric[i] ? "number" : "word").arg(maxLen));
        }
        if (!ok) break;
        vals << v;
    }

    // Create row with all user-provided values
    int newRow = m_ecoModel->rowCount();
    m_ecoModel->addRowWithFullData(ecoName, vals);
    m_ecoDirty = true;
    
    // Focus on the new row
    QModelIndex newIdx = m_ecoProxy->mapFromSource(
        m_ecoModel->index(newRow, EcoTableModel::COL_ECONUM));
    if (newIdx.isValid())
        m_ecoView->setCurrentIndex(newIdx);
}

void MainWindow::onEcoDelete()
{
    QModelIndex idx = m_ecoProxy->mapToSource(activeIndex(m_ecoView));
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
    QModelIndex idx = m_ecoProxy->mapToSource(activeIndex(m_ecoView));
    if (!idx.isValid()) {
        setStatus("Select an ecotype row to duplicate.", true);
        return;
    }
    int newRow = m_ecoModel->rowCount();
    m_ecoModel->duplicateRow(idx.row());
    if (m_ecoModel->rowCount() == newRow) return;
    QModelIndex newIdx = m_ecoProxy->mapFromSource(m_ecoModel->index(newRow, 0));
    if (newIdx.isValid()) {
        m_ecoView->setCurrentIndex(newIdx);
        m_ecoView->scrollTo(newIdx);
    }
    setStatus(QString("Duplicated %1 as %2.")
                  .arg(m_ecoModel->index(idx.row(), 0).data().toString().trimmed(),
                       m_ecoModel->index(newRow, 0).data().toString().trimmed()));
}

void MainWindow::onEcoSave()
{
    if (m_currentEcoPath.isEmpty()) return;
    BackupManager::createBackup(m_currentEcoPath);
    if (EcoParser::write(m_currentEcoPath, m_ecoModel->rows(), m_ecoHeaderLines,
                         m_ecoModel->layout(), m_ecoFooterLines)) {
        m_ecoDirty = false;
        setStatus("ECO saved: " + m_currentEcoPath);
        m_ecoSaveFailed = false;
    } else {
        m_ecoSaveFailed = true;
        setStatus("Failed to save: " + m_currentEcoPath, true);
    }
}

void MainWindow::onEcoSearch(const QString &text)
{
    if (m_ecoProxy)
        m_ecoProxy->setFilterFixedString(text);
}

// ─── Auto-save ───────────────────────────────────────────────────────────────
void MainWindow::updateSaveIndicators()
{
    auto show = [](QLabel *l, bool dirty, bool failed) {
        if (!l) return;
        const QString state = failed ? "failed" : dirty ? "saving" : "saved";
        if (l->property("state").toString() == state) return;
        l->setProperty("state", state);
        if (failed) {
            l->setText("\u26A0 Not saved");
            l->setStyleSheet("color: #C62828; font-weight: bold;");
        } else if (dirty) {
            l->setText("Saving\u2026");
            l->setStyleSheet("color: #6B7178;");
        } else {
            l->setText("\u2713 Saved");
            l->setStyleSheet("color: #2E7D32;");
        }
    };
    show(m_culSaveState, m_culDirty, m_culSaveFailed);
    show(m_ecoSaveState, m_ecoDirty, m_ecoSaveFailed);
    show(m_speSaveState, m_speDirty, m_speSaveFailed);
}

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
    if (SpeEditor::save(m_currentSpePath, m_speEdit->toPlainText())) {
        m_speDirty = false;
        setStatus("SPE saved: " + m_currentSpePath);
        m_speSaveFailed = false;
    } else {
        m_speSaveFailed = true;
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
        setStatus(QString("Found \"%1\" at line %2").arg(needle).arg(cur.blockNumber() + 1));
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

    // The scroll handler reads the line in the 8px margin, which belongs to the
    // previous section; keep the clicked section selected.
    const auto items = m_speNavList->findItems(section, Qt::MatchExactly);
    if (!items.isEmpty()) {
        m_speNavList->blockSignals(true);
        m_speNavList->setCurrentItem(items.first());
        m_speNavList->blockSignals(false);
    }
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
    // Graph logic lives in SpeGraphBuilder so --spe-report can check it on every SPE file
    const QTextDocument *doc = m_speEdit->document();
    QStringList lines;
    lines.reserve(doc->blockCount());
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        lines << b.text();

    SpeGraphData data;
    if (SpeGraphBuilder::build(lines, m_speEdit->textCursor().blockNumber(), data))
        m_speGraphWidget->setData(data);
    else
        m_speGraphWidget->clearData();
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

void MainWindow::onOpenRDir()
{
    const QString current = GlueRunner::rDir();
    const QString inUse   = GlueRunner::findRTerm();

    // Offer a way back to auto-detect once a directory has been chosen
    if (!current.isEmpty()) {
        QMessageBox box(QMessageBox::Question, "R directory",
            QString("R used for GLUE:\n%1\n\nSelected directory:\n%2")
                .arg(QDir::toNativeSeparators(inUse), QDir::toNativeSeparators(current)),
            QMessageBox::NoButton, this);
        QPushButton *chooseBtn = box.addButton("Choose another…", QMessageBox::AcceptRole);
        QPushButton *autoBtn   = box.addButton("Use auto-detect", QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == autoBtn) {
            GlueRunner::setRDir(QString());
            setStatus("R directory: auto-detect (" +
                      QDir::toNativeSeparators(GlueRunner::findRTerm()) + ")");
            return;
        }
        if (box.clickedButton() != chooseBtn) return;
    }

#ifdef Q_OS_WIN
    const QString startDir = current.isEmpty() ? QString("C:/Program Files/R") : current;
    const QString hint = "the R install folder, e.g. C:\\Program Files\\R\\R-4.6.1";
#else
    const QString startDir = current.isEmpty() ? QString("/Library/Frameworks/R.framework") : current;
    const QString hint = "the R install folder (containing bin/Rscript)";
#endif
    QString dir = QFileDialog::getExistingDirectory(this, "Select R directory", startDir);
    if (dir.isEmpty()) return;

    const QString exe = GlueRunner::rExecutableIn(dir);
    if (exe.isEmpty()) {
        QMessageBox::warning(this, "R directory",
            "R was not found in:\n" + QDir::toNativeSeparators(dir) +
            "\n\nPlease select " + hint + ".");
        return;
    }
    GlueRunner::setRDir(dir);
    setStatus("R directory set: " + QDir::toNativeSeparators(exe));
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

void MainWindow::onCulHeaderContextMenu(const QPoint &pos)
{
    showCalibrationMenu(m_culView->horizontalHeader()->logicalIndexAt(pos),
                        m_culView->horizontalHeader()->mapToGlobal(pos));
}

void MainWindow::showCalibrationMenu(int col, const QPoint &globalPos)
{
    if (col < CulTableModel::COL_PARAM0) return;

    QString paramName = m_culModel->columnName(col);
    QString current = m_culModel->calibrationTypes().value(paramName);

    QMenu menu(this);
    menu.setTitle(QString("Calibration type: %1").arg(paramName));

    QAction *pAct = menu.addAction("P — Phenology");
    QAction *gAct = menu.addAction("G — Growth");
    QAction *nAct = menu.addAction("N — Not calibrated");

    pAct->setCheckable(true); pAct->setChecked(current == "P");
    gAct->setCheckable(true); gAct->setChecked(current == "G");
    nAct->setCheckable(true); nAct->setChecked(current == "N");

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;

    QString newType;
    if (chosen == pAct) newType = "P";
    else if (chosen == gAct) newType = "G";
    else newType = "N";

    if (newType == current) return;

    m_culModel->setCalibrationTypeForParam(paramName, newType);
    updateCalibrationHeaderLine();
    m_culDirty = true;
    m_autoSaveTimer->start();
    updateSaveIndicators();
    setStatus(QString("Calibration type for %1 set to %2.").arg(paramName, newType));
}

void MainWindow::updateCalibrationHeaderLine()
{
    QStringList paramNames = CulParser::extractParamNames(m_culHeaderLines);
    if (paramNames.isEmpty()) return;

    const QMap<QString, QString> &types = m_culModel->calibrationTypes();

    // Each letter under the last character of its column name in the "@VAR#"
    // line, as DSSAT's own files do. GLUE only splits the line on spaces.
    int headerIdx = -1;
    for (int i = 0; i < m_culHeaderLines.size(); ++i)
        if (m_culHeaderLines[i].startsWith("@VAR")) { headerIdx = i; break; }

    QString calLine = "!Calibration";
    QVector<int> ends;
    if (headerIdx >= 0) {
        static const QRegularExpression tok("\\S+");
        auto it = tok.globalMatch(m_culHeaderLines[headerIdx]);
        QStringList names;
        while (it.hasNext()) {
            const auto m = it.next();
            names << m.captured();
            ends << m.capturedEnd() - 1;
        }
        // @VAR#, VRNAME..., EXPNO, ECO# come before the parameters
        if (names.size() >= 4 + paramNames.size() && names.mid(4, paramNames.size()) == paramNames)
            ends = ends.mid(4, paramNames.size());
        else
            ends.clear();
    }
    for (int k = 0; k < paramNames.size(); ++k) {
        const QString t = types.value(paramNames[k], "N");
        if (!ends.isEmpty() && ends[k] > calLine.size())
            calLine = calLine.leftJustified(ends[k], ' ') + t;
        else
            calLine += "  " + t;
    }

    static const QRegularExpression calRe("^!\\s*[Cc]alibration\\b");
    bool found = false;
    for (int i = 0; i < m_culHeaderLines.size(); ++i) {
        if (calRe.match(m_culHeaderLines[i]).hasMatch()) {
            m_culHeaderLines[i] = calLine;
            found = true;
            break;
        }
    }
    if (!found && headerIdx >= 0)   // right under "@VAR#", where DSSAT's files have it
        m_culHeaderLines.insert(headerIdx + 1, calLine);
}
