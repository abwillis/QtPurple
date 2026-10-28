/*
 * QtPurple - a Qt front end for libpurple
 *
 * Buddy list main window and PurpleBlistUiOps.
 *
 * The tree is rebuilt from libpurple's buddy list (coalesced) whenever
 * libpurple reports a change. Tree items only ever hold pointers to nodes
 * that are alive: when libpurple removes a node that is displayed, the tree
 * is cleared synchronously inside the remove callback before the node is
 * freed, and a rebuild is scheduled.
 */
#ifndef QTPURPLE_BLISTWINDOW_H
#define QTPURPLE_BLISTWINDOW_H

#include "common.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class BuddyListWindow : public QMainWindow
{
    Q_OBJECT
public:
    static BuddyListWindow *instance();
    static PurpleBlistUiOps *uiOps();
    static bool nodeExists(PurpleBlistNode *node);

    void applyPrefs();
    void saveGeometryPrefs();
    void showStatusMessage(const QString &message, int timeoutMs = 0);

    void showAddBuddyDialog(PurpleAccount *account, const QString &user,
                            const QString &group, const QString &alias);
    void showAddChatDialog(PurpleAccount *account, const QString &group,
                           const QString &alias, const QString &name);
    void showAddGroupDialog();
    void showJoinChatDialog();
    void showNewImDialog();
    void showGetInfoDialog();

    /* Called from the blist ui ops. */
    void scheduleRebuild(int delayMs = 50);
    void nodeRemoved(PurpleBlistNode *node);
    void listDestroyed();
    void syncStatusBox();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void rebuild();
    void onItemActivated(QTreeWidgetItem *item, int column);
    void onContextMenu(const QPoint &pos);
    void onItemExpanded(QTreeWidgetItem *item);
    void onItemCollapsed(QTreeWidgetItem *item);
    void onStatusChosen(int index);
    void onStatusMessageEdited();
    void populateAccountsMenu();

private:
    BuddyListWindow();

    enum ItemKind { KindGroup = 1, KindContact, KindBuddy, KindChat };

    void buildMenus();
    void buildStatusBox();
    void applyStatus();

    QTreeWidgetItem *makeContactItem(PurpleContact *contact, bool showOffline);
    QTreeWidgetItem *makeBuddyItem(PurpleBuddy *buddy, ItemKind kind, PurpleBlistNode *node);
    QTreeWidgetItem *makeChatItem(PurpleChat *chat);
    static PurpleBlistNode *itemNode(const QTreeWidgetItem *item);
    static ItemKind itemKind(const QTreeWidgetItem *item);
    PurpleBlistNode *currentNode() const;
    QString tooltipFor(PurpleBlistNode *node) const;

    void openIm(PurpleBuddy *buddy);
    void joinChat(PurpleChat *chat);
    void buddyMenu(QMenu *menu, PurpleBuddy *buddy, PurpleContact *contact);
    void groupMenu(QMenu *menu, PurpleGroup *group);
    void chatMenu(QMenu *menu, PurpleChat *chat);
    void appendProtocolMenu(QMenu *menu, PurpleBlistNode *node, PurpleAccount *account);
    void removeContact(PurpleContact *contact);
    void removeBuddy(PurpleBuddy *buddy);
    void removeGroup(PurpleGroup *group);
    void removeChat(PurpleChat *chat);
    void freePluginActions();

    QTreeWidget *m_tree;
    QComboBox *m_status;
    QLineEdit *m_statusMessage;
    QLabel *m_statusLabel;
    QMenu *m_accountsMenu;
    QAction *m_manageAccounts;
    QAction *m_showOffline;
    QAction *m_showEmpty;
    QTimer *m_rebuildTimer;

    QHash<PurpleBlistNode *, QTreeWidgetItem *> m_items;
    QSet<PurpleBlistNode *> m_expandedContacts;
    QList<PurplePluginAction *> m_pluginActions;
    QList<QMenu *> m_accountSubmenus;
    bool m_dead = false;
    bool m_building = false;
    bool m_syncingStatus = false;
};

#endif
