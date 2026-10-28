/*
 * QtPurple - a Qt front end for libpurple
 *
 * Room list browser and PurpleRoomlistUiOps.
 *
 * Ownership: purple_roomlist_get_list() returns a list holding one
 * reference for the caller. The dialog keeps that reference while it shows
 * the list and drops it (after cancelling a running fetch) when it fetches
 * another list or closes. Room pointers are valid until libpurple calls the
 * destroy op, where the tree is cleared.
 */
#ifndef QTPURPLE_ROOMLISTDIALOG_H
#define QTPURPLE_ROOMLISTDIALOG_H

#include "common.h"

#include <QDialog>
#include <QHash>

class AccountCombo;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class RoomListDialog : public QDialog
{
    Q_OBJECT
public:
    static PurpleRoomlistUiOps *uiOps();
    static void showDialog(PurpleAccount *account = nullptr, bool fetch = false);

    /* Called from the ui ops for the list this dialog owns. */
    void setFields(GList *fields);
    void addRoom(PurpleRoomlistRoom *room);
    void setInProgress(bool on);
    void listDestroyed(PurpleRoomlist *list);

    ~RoomListDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void fetch();
    void stop();
    void join();
    void addChat();
    void onAccountChanged(PurpleAccount *account);
    void onSelectionChanged();
    void onItemExpanded(QTreeWidgetItem *item);
    void applyFilter();

private:
    explicit RoomListDialog(QWidget *parent = nullptr);
    void releaseList();
    PurpleRoomlistRoom *selectedRoom() const;
    bool listUsable() const;

    AccountCombo *m_account;
    QLineEdit *m_filter;
    QTreeWidget *m_tree;
    QProgressBar *m_progress;
    QLabel *m_count;
    QPushButton *m_fetch;
    QPushButton *m_stop;
    QPushButton *m_join;
    QPushButton *m_add;

    PurpleRoomlist *m_list = nullptr;
    QList<int> m_columns;                           /* field index per extra column */
    QHash<PurpleRoomlistRoom *, QTreeWidgetItem *> m_items;
    int m_rooms = 0;
};

#endif
