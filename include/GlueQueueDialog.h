#ifndef GLUEQUEUEDIALOG_H
#define GLUEQUEUEDIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include "DssatProParser.h"
#include "GlueRunner.h"
#include "GlueQueueManager.h"

// Dialog for selecting treatments + GLUE params before adding to the queue
class GlueQueueDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlueQueueDialog(const CropInfo &cropInfo,
                             const QString  &cultivarId,
                             const QString  &cultivarName,
                             QWidget *parent = nullptr);

    GlueQueueEntry result() const { return m_entry; }

private slots:
    void onSelectAll();
    void onUnselectAll();
    void onAddToQueue();

private:
    void scan();

    CropInfo m_cropInfo;
    QString  m_cultivarId;
    QString  m_cultivarName;

    QTreeWidget  *m_tree;
    QLabel       *m_statusLabel;
    QSpinBox     *m_runsSpin;
    QComboBox    *m_modeCombo;
    QCheckBox    *m_ecoCheck;
    QPushButton  *m_addBtn;

    GlueQueueEntry m_entry;
};

#endif // GLUEQUEUEDIALOG_H
