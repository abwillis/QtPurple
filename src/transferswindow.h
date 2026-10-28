/*
 * QtPurple - a Qt front end for libpurple
 *
 * File transfer window and PurpleXferUiOps.
 */
#ifndef QTPURPLE_TRANSFERSWINDOW_H
#define QTPURPLE_TRANSFERSWINDOW_H

#include "common.h"

#include <QHash>
#include <QWidget>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class TransfersWindow : public QWidget
{
    Q_OBJECT
public:
    static TransfersWindow *instance();
    static PurpleXferUiOps *uiOps();

    void present();
    void addXfer(PurpleXfer *xfer);
    void updateXfer(PurpleXfer *xfer);
    void xferDestroyed(PurpleXfer *xfer);

private Q_SLOTS:
    void onSelectionChanged();
    void onCancel();
    void onClearFinished();
    void onOpenFolder();

private:
    TransfersWindow();
    PurpleXfer *selectedXfer() const;

    QTreeWidget *m_tree;
    QPushButton *m_cancel;
    QPushButton *m_open;
    QHash<PurpleXfer *, QTreeWidgetItem *> m_items;
};

#endif
