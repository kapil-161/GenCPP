#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTableView>
#include <QTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QSplitter>
#include <QCheckBox>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QMap>
#include <QStringList>
#include <memory>

#include "DssatProParser.h"
#include "DetailCdeParser.h"
#include "CulParser.h"
#include "EcoParser.h"
#include "CulTableModel.h"
#include "EcoTableModel.h"
#include "SpeGraphWidget.h"
#include "GlueQueueManager.h"
#include "GlueQueuePanel.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Menu actions
    void onOpenDssatDir();
    void onOpenGlueDir();
    void onAbout();

    // Crop/file selection
    void onCropChanged(int index);

    // CUL tab buttons
    void onCulAdd();
    void onCulDelete();
    void onCulDuplicate();
    void onCulSave();
    void onCulRefresh();
    void onCulShowUsed(bool checked);
    void onCulSearch(const QString &text);
    void onCulPasteGlue();
    void onCulCopyRow();

    // ECO tab buttons
    void onEcoAdd();
    void onEcoDelete();
    void onEcoDuplicate();
    void onEcoSave();
    void onEcoSearch(const QString &text);
    void onEcoCopyRow();

    // SPE tab buttons
    void onSpeSave();
    void onSpeSearch();
    void onSpeSectionClicked(const QString &section);
    void onSpeScrolled(int value);
    void onSpeCursorPositionChanged();

    // Auto-save
    void autoSaveAll();

private:
    void setupUI();
    void setupMenuBar();
    void setupCropBar();
    void setupCulTab(QWidget *tab);
    void setupEcoTab(QWidget *tab);
    void setupSpeTab(QWidget *tab);
    void connectSignals();

    void loadDssatConfig(const QString &dssatDir);
    void loadCrop(const QString &cropCode);
    void loadFileType(const QString &fileType);
    void refreshEcoCrossRef();
    void buildSpeNavigator();
    void setStatus(const QString &msg, bool error = false);

    // UI – top bar
    QLineEdit  *m_dssatDirEdit;
    QPushButton *m_browseButton;
    QComboBox  *m_cropCombo;
    QLabel     *m_geneticsLabel;

    // Tab widget
    QTabWidget *m_tabWidget;

    // CUL tab
    QLineEdit  *m_culSearch;
    QTableView *m_culView;
    CulTableModel *m_culModel;
    class CulSortProxy *m_culProxy = nullptr;
    QPushButton *m_culAddBtn, *m_culDelBtn, *m_culDupBtn;
    QPushButton *m_culSaveBtn, *m_culRefreshBtn, *m_culShowUsedBtn;
    QPushButton *m_culGlueQueueBtn = nullptr;
    bool        m_culDirty = false;

    // ECO tab
    QLineEdit  *m_ecoSearch;
    QTableView *m_ecoView;
    EcoTableModel *m_ecoModel;
    QSortFilterProxyModel *m_ecoProxy = nullptr;
    QPushButton *m_ecoAddBtn, *m_ecoDelBtn, *m_ecoDupBtn, *m_ecoSaveBtn;
    bool        m_ecoDirty = false;

    // SPE tab
    QTextEdit   *m_speEdit;
    QListWidget *m_speNavList;
    QLineEdit   *m_speSearchEdit;
    QPushButton *m_speSaveBtn;
    SpeGraphWidget *m_speGraphWidget;
    bool         m_speDirty = false;

    // GLUE queue
    GlueQueueManager *m_glueQueue = nullptr;
    GlueQueuePanel   *m_gluePanel = nullptr;

    // Auto-save timer
    QTimer *m_autoSaveTimer = nullptr;

    // Status bar
    QLabel *m_statusLabel;

    // Data
    QMap<QString, CropInfo>  m_crops;
    QMap<QString, QMap<QString, QString>> m_cdeData;
    QString m_currentCropCode;
    QString m_currentCulPath;
    QString m_currentEcoPath;
    QString m_currentSpePath;
    QStringList m_culHeaderLines;
    QStringList m_ecoHeaderLines;
};

#endif // MAINWINDOW_H
