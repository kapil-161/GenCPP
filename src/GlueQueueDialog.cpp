#include "GlueQueueDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QLabel>

GlueQueueDialog::GlueQueueDialog(const CropInfo &cropInfo,
                                 const QString  &cultivarId,
                                 const QString  &cultivarName,
                                 QWidget *parent)
    : QDialog(parent)
    , m_cropInfo(cropInfo)
    , m_cultivarId(cultivarId)
    , m_cultivarName(cultivarName)
{
    setWindowTitle(QString("Add to GLUE Queue — %1 / %2 %3")
                   .arg(cropInfo.cropCode, cultivarId, cultivarName));
    setMinimumSize(600, 400);

    QVBoxLayout *main = new QVBoxLayout(this);

    // Title
    QLabel *title = new QLabel(
        QString("<b>Select Treatments</b><br>"
                "<small>Crop: %1 &nbsp; Cultivar: %2 %3</small>")
        .arg(cropInfo.cropCode, cultivarId, cultivarName));
    main->addWidget(title);

    // Tree + select buttons
    QHBoxLayout *treeRow = new QHBoxLayout;
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    treeRow->addWidget(m_tree, 1);

    QVBoxLayout *treeBtns = new QVBoxLayout;
    treeBtns->setAlignment(Qt::AlignTop);
    QPushButton *selAll   = new QPushButton("Select All");
    QPushButton *unselAll = new QPushButton("Unselect All");
    treeBtns->addWidget(selAll);
    treeBtns->addWidget(unselAll);
    treeRow->addLayout(treeBtns);
    main->addLayout(treeRow, 1);

    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    main->addWidget(m_statusLabel);

    // GLUE params
    QGroupBox *paramBox = new QGroupBox("GLUE Parameters");
    QFormLayout *form = new QFormLayout(paramBox);

    m_runsSpin = new QSpinBox;
    m_runsSpin->setRange(100, 1000000);
    m_runsSpin->setValue(500);
    m_runsSpin->setSingleStep(1000);
    form->addRow("Runs:", m_runsSpin);

    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("Both (Phenology + Growth)", 1);
    m_modeCombo->addItem("Phenology Only",            2);
    m_modeCombo->addItem("Growth Parameters",         3);
    form->addRow("Mode:", m_modeCombo);

    m_ecoCheck = new QCheckBox;
    form->addRow("Include ECO file:", m_ecoCheck);

    main->addWidget(paramBox);

    // Bottom buttons
    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_addBtn = new QPushButton("Add to Queue");
    m_addBtn->setStyleSheet("font-weight:bold; background:#2196F3; color:white;");
    m_addBtn->setDefault(true);
    QPushButton *cancelBtn = new QPushButton("Cancel");
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(cancelBtn);
    main->addLayout(btnRow);

    connect(selAll,    &QPushButton::clicked, this, &GlueQueueDialog::onSelectAll);
    connect(unselAll,  &QPushButton::clicked, this, &GlueQueueDialog::onUnselectAll);
    connect(m_addBtn,  &QPushButton::clicked, this, &GlueQueueDialog::onAddToQueue);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        QTreeWidgetItem *par = item->parent();
        if (!par) return;
        m_tree->blockSignals(true);
        bool any = false;
        for (int i = 0; i < par->childCount(); ++i)
            if (par->child(i)->checkState(0) == Qt::Checked) { any = true; break; }
        par->setCheckState(0, any ? Qt::Checked : Qt::Unchecked);
        m_tree->blockSignals(false);
    });

    scan();
}

void GlueQueueDialog::scan()
{
    m_tree->clear();
    ScanResult res = GlueRunner::scanExperiments(m_cropInfo, m_cultivarId);

    if (!res.errorMsg.isEmpty()) {
        m_statusLabel->setText(res.errorMsg);
        m_statusLabel->setStyleSheet("padding:4px 8px; background:#F44336; color:white; border-radius:3px;");
        m_statusLabel->show();
        m_addBtn->setEnabled(false);
        return;
    }

    for (auto it = res.treatments.begin(); it != res.treatments.end(); ++it) {
        QTreeWidgetItem *par = new QTreeWidgetItem(m_tree);
        par->setText(0, it.key());
        par->setCheckState(0, Qt::Unchecked);
        par->setExpanded(true);
        for (const TreatmentEntry &e : it.value()) {
            QTreeWidgetItem *child = new QTreeWidgetItem(par);
            child->setText(0, QString("[%1] %2").arg(e.number).arg(e.name));
            child->setCheckState(0, Qt::Unchecked);
        }
    }

    if (m_tree->topLevelItemCount() == 0) {
        QString xExt = m_cropInfo.cropCode + "X";
        QString msg = res.filesScanned == 0
            ? QString("No .%1 files found in: %2").arg(xExt, m_cropInfo.expDir)
            : QString("Cultivar %1 not found in any experiment file.").arg(m_cultivarId);
        m_statusLabel->setText(msg);
        m_statusLabel->setStyleSheet("padding:4px 8px; background:#FF9800; color:white; border-radius:3px;");
        m_statusLabel->show();
        m_addBtn->setEnabled(false);
    }
}

void GlueQueueDialog::onSelectAll()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *par = m_tree->topLevelItem(i);
        par->setCheckState(0, Qt::Checked);
        for (int j = 0; j < par->childCount(); ++j)
            par->child(j)->setCheckState(0, Qt::Checked);
    }
}

void GlueQueueDialog::onUnselectAll()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *par = m_tree->topLevelItem(i);
        par->setCheckState(0, Qt::Unchecked);
        for (int j = 0; j < par->childCount(); ++j)
            par->child(j)->setCheckState(0, Qt::Unchecked);
    }
}

void GlueQueueDialog::onAddToQueue()
{
    TreatmentMap selected;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *expItem = m_tree->topLevelItem(i);
        QString filePath = expItem->text(0);
        for (int j = 0; j < expItem->childCount(); ++j) {
            QTreeWidgetItem *trtItem = expItem->child(j);
            if (trtItem->checkState(0) != Qt::Checked) continue;
            QString txt = trtItem->text(0);
            int from = txt.indexOf('[') + 1, to = txt.indexOf(']');
            if (from > 0 && to > from) {
                TreatmentEntry e;
                e.number = txt.mid(from, to - from).toInt();
                e.name   = txt.mid(to + 2).trimmed();
                selected[filePath] << e;
            }
        }
    }

    if (selected.isEmpty()) {
        QMessageBox::warning(this, "No selection", "Please select at least one treatment.");
        return;
    }

    m_entry.cultivarId         = m_cultivarId;
    m_entry.cultivarName       = m_cultivarName;
    m_entry.cropInfo           = m_cropInfo;
    m_entry.selectedTreatments = selected;
    m_entry.runs               = m_runsSpin->value();
    m_entry.glueFlag           = m_modeCombo->currentData().toInt();
    m_entry.ecoCalib           = m_ecoCheck->isChecked() ? "Y" : "N";
    m_entry.status             = GlueQueueStatus::Pending;

    accept();
}
