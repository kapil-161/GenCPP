#include "GlueQueuePanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QProcess>
#include <QFont>

GlueQueuePanel::GlueQueuePanel(GlueQueueManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    QVBoxLayout *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // Table
    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({"VAR#", "Name", "Treatments", "Runs", "Mode", "Status"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->verticalHeader()->hide();
    m_table->setToolTip("Double-click a Done row to view outputs");
    vbox->addWidget(m_table, 1);

    // Progress bar (shown when running)
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(true);
    m_progressBar->hide();
    vbox->addWidget(m_progressBar);

    m_progressLabel = new QLabel;
    m_progressLabel->hide();
    vbox->addWidget(m_progressLabel);

    // Buttons
    QHBoxLayout *btnRow = new QHBoxLayout;
    m_removeBtn    = new QPushButton("Remove Selected");
    m_clearDoneBtn = new QPushButton("Clear Done");
    btnRow->addWidget(m_removeBtn);
    btnRow->addWidget(m_clearDoneBtn);
    btnRow->addStretch();
    vbox->addLayout(btnRow);

    connect(m_removeBtn,    &QPushButton::clicked, this, &GlueQueuePanel::onRemove);
    connect(m_clearDoneBtn, &QPushButton::clicked, this, &GlueQueuePanel::onClearDone);

    connect(manager, &GlueQueueManager::queueChanged,   this, &GlueQueuePanel::refresh);
    connect(manager, &GlueQueueManager::progressUpdated,
            this, [this](int /*index*/, int pct, const QString &label) {
        m_progressBar->setValue(pct);
        m_progressLabel->setText(label);
    });

    connect(m_table, &QTableWidget::cellDoubleClicked, this, &GlueQueuePanel::onRowDoubleClicked);
}

void GlueQueuePanel::refresh()
{
    const auto &entries = m_manager->entries();
    m_table->setRowCount(entries.size());

    bool anyRunning = false;
    for (int i = 0; i < entries.size(); ++i) {
        const GlueQueueEntry &e = entries[i];

        m_table->setItem(i, 0, new QTableWidgetItem(e.cultivarId));
        m_table->setItem(i, 1, new QTableWidgetItem(e.cultivarName));
        m_table->setItem(i, 2, new QTableWidgetItem(
            QString("%1 treatment(s)").arg(e.selectedTreatments.size())));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(e.runs)));

        QString modeStr;
        switch (e.glueFlag) {
            case 2:  modeStr = "Phenology"; break;
            case 3:  modeStr = "Growth";    break;
            default: modeStr = "Both";      break;
        }
        m_table->setItem(i, 4, new QTableWidgetItem(modeStr));

        QString statusStr;
        switch (e.status) {
            case GlueQueueStatus::Pending: statusStr = "Pending";  break;
            case GlueQueueStatus::Running: statusStr = "Running…"; anyRunning = true; break;
            case GlueQueueStatus::Done:    statusStr = "✓ Done (double-click to view)"; break;
            case GlueQueueStatus::Failed:  statusStr = "✗ Failed (double-click for log): " + e.errorMsg; break;
        }
        auto *statusItem = new QTableWidgetItem(statusStr);
        if (e.status == GlueQueueStatus::Done)
            statusItem->setForeground(Qt::darkGreen);
        else if (e.status == GlueQueueStatus::Failed)
            statusItem->setForeground(Qt::red);
        else if (e.status == GlueQueueStatus::Running)
            statusItem->setForeground(QColor("#2196F3"));
        m_table->setItem(i, 5, statusItem);
    }

    m_progressBar->setVisible(anyRunning);
    m_progressLabel->setVisible(anyRunning);
    if (!anyRunning) {
        m_progressBar->setValue(0);
        m_progressLabel->clear();
    }
}

void GlueQueuePanel::onRemove()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    const auto &entries = m_manager->entries();
    if (row >= entries.size()) return;
    if (entries[row].status == GlueQueueStatus::Running) return;
    m_manager->removeEntry(row);
}

void GlueQueuePanel::onClearDone()
{
    m_manager->clearDone();
}

void GlueQueuePanel::onRowDoubleClicked(int row, int /*col*/)
{
    const auto &entries = m_manager->entries();
    if (row < 0 || row >= entries.size()) return;
    const GlueQueueEntry &e = entries[row];
    if (e.status != GlueQueueStatus::Done && e.status != GlueQueueStatus::Failed) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("GLUE Results — %1 %2").arg(e.cultivarId, e.cultivarName));
    dlg.resize(750, 500);

    QVBoxLayout *vl = new QVBoxLayout(&dlg);

    QTabWidget *tabs = new QTabWidget;

    // Tab 1: Cultivar Coefficients — plain text with header + result line
    QWidget *coeffTab = new QWidget;
    QVBoxLayout *coeffLayout = new QVBoxLayout(coeffTab);

    // Load header lines from snapshot CUL file
    QString headerLine, calibLine;
    if (!e.snapshotDir.isEmpty()) {
        QFile hf(e.snapshotDir + "/" + e.cropInfo.module + ".CUL");
        if (hf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &hl : QString::fromLatin1(hf.readAll()).split('\n')) {
                if (hl.startsWith("@VAR#")) { headerLine = hl.trimmed(); break; }
            }
            hf.close();
        }
    }
    calibLine = e.resultCulLine;

    QTextEdit *coeffEdit = new QTextEdit;
    coeffEdit->setReadOnly(true);
    coeffEdit->setFont(QFont("Courier New", 9));
    coeffEdit->setWordWrapMode(QTextOption::NoWrap);
    if (!headerLine.isEmpty())
        coeffEdit->setPlainText(headerLine + "\n" + calibLine);
    else
        coeffEdit->setPlainText(calibLine);
    coeffLayout->addWidget(coeffEdit, 1);

    QHBoxLayout *lineRow = new QHBoxLayout;
    lineRow->addStretch();
    QPushButton *copyBtn = new QPushButton("Copy Line");
    copyBtn->setFixedWidth(80);
    connect(copyBtn, &QPushButton::clicked, this, [culLine = e.resultCulLine]() {
        QGuiApplication::clipboard()->setText(culLine);
    });
    lineRow->addWidget(copyBtn);
    coeffLayout->addLayout(lineRow);

    tabs->addTab(coeffTab, "Genotype Coefficients");

    // Helper to load a file from snapshot
    auto loadFile = [&](const QString &fileName) -> QString {
        if (e.snapshotDir.isEmpty()) return "(snapshot not available)";
        QFile f(e.snapshotDir + "/" + fileName);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString("(file not found: %1)").arg(fileName);
        return QString::fromLatin1(f.readAll());
    };

    // Tab 2: Development (ModelRunIndicator.txt)
    QTextEdit *devEdit = new QTextEdit;
    devEdit->setReadOnly(true);
    devEdit->setFont(QFont("Courier New", 8));
    devEdit->setPlainText(loadFile("ModelRunIndicator.txt"));
    tabs->addTab(devEdit, "Development");

    // Tab 3: Growth and Yield (EvaluateFrame_2.txt)
    QTextEdit *yieldEdit = new QTextEdit;
    yieldEdit->setReadOnly(true);
    yieldEdit->setFont(QFont("Courier New", 8));
    yieldEdit->setPlainText(loadFile("EvaluateFrame_2.txt"));
    tabs->addTab(yieldEdit, "Growth and Yield");

    // Tab 4: Warnings
    QTextEdit *logEdit = new QTextEdit;
    logEdit->setReadOnly(true);
    logEdit->setFont(QFont("Courier New", 8));
    QString logText;
    if (e.status == GlueQueueStatus::Failed) {
        logText = loadFile("GlueWarning.txt");
        if (logText.startsWith("(file not found")) logText = loadFile("GLwork.txt");
        if (!e.errorMsg.isEmpty())
            logText = "Error: " + e.errorMsg + "\n\n" + logText;
    } else {
        logText = "(No warnings — calibration completed successfully)";
    }
    logEdit->setPlainText(logText);
    tabs->addTab(logEdit, "Warnings");
    if (e.status == GlueQueueStatus::Failed)
        tabs->setCurrentWidget(logEdit);

    vl->addWidget(tabs, 1);

    QPushButton *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    vl->addLayout(btnRow);

    dlg.exec();
}
