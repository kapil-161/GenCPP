#ifndef GLUEWIZARD_H
#define GLUEWIZARD_H

#include <QDialog>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QProcess>
#include <QStringList>
#include <QProgressBar>
#include <QTimer>
#include "DssatProParser.h"

class GlueWizard : public QDialog
{
    Q_OBJECT

public:
    explicit GlueWizard(const CropInfo &cropInfo,
                        const QString &cultivarId,
                        const QString &cultivarName,
                        QWidget *parent = nullptr);

signals:
    void cultivarCalibrated(const QString &culLine);

private slots:
    void onGoFromTreatments();
    void onBackToTreatments();
    void onSelectAll();
    void onUnselectAll();
    void onBrowseBackup();
    void onRunGlue();
    void onStopGlue();
    void onStartOver();
    void onGlueOutput();
    void onGlueFinished(int exitCode);
    void onPollProgress();

private:
    void setupTreatmentPage();
    void setupBackupPage();
    void setupRunPage();
    void scanExperiments();
    QStringList selectedTreatmentFiles();

    // Data
    CropInfo m_cropInfo;
    QString  m_cultivarId;
    QString  m_cultivarName;

    // Pages
    QStackedWidget *m_stack;

    // Page 1 — treatment tree
    QTreeWidget  *m_tree;
    QPushButton  *m_selectAllBtn;
    QPushButton  *m_unselectAllBtn;
    QPushButton  *m_goBtn;

    // Page 2 — backup
    QLineEdit    *m_backupDirEdit;
    QPushButton  *m_backupBrowseBtn;
    QPushButton  *m_backupYesBtn;
    QPushButton  *m_backupNoBtn;

    // Page 3 — run
    QSpinBox     *m_runsSpin;
    QComboBox    *m_modeCombo;
    QCheckBox    *m_ecoCheck;
    QPushButton  *m_runGlueBtn;
    QPushButton  *m_stopGlueBtn;
    QPushButton  *m_startOverBtn;
    QPushButton  *m_outCoeffBtn;
    QPushButton  *m_outDevBtn;
    QPushButton  *m_outYieldBtn;
    QTextEdit    *m_logEdit;
    QProgressBar *m_progressBar;
    QLabel       *m_progressLabel;

    // Process
    QProcess     *m_glueProcess = nullptr;
    QTimer       *m_pollTimer   = nullptr;
    QStringList   m_selectedFiles;
    int           m_lastIndicatorLine = 0;
    int           m_totalRuns = 0;
    int           m_glueRound = 0;
};

#endif // GLUEWIZARD_H
