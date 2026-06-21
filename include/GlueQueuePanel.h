#ifndef GLUEQUEUEPANEL_H
#define GLUEQUEUEPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include "GlueQueueManager.h"

class GlueQueuePanel : public QWidget
{
    Q_OBJECT

public:
    explicit GlueQueuePanel(GlueQueueManager *manager, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onRemove();
    void onClearDone();
    void onRowDoubleClicked(int row, int col);

private:
    GlueQueueManager *m_manager;
    QTableWidget     *m_table;
    QPushButton      *m_removeBtn;
    QPushButton      *m_clearDoneBtn;
    QProgressBar     *m_progressBar;
    QLabel           *m_progressLabel;
};

#endif // GLUEQUEUEPANEL_H
