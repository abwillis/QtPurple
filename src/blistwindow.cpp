/*
 * QtPurple - a Qt front end for libpurple
 *
 * Buddy list main window and PurpleBlistUiOps.
 */
#include "blistwindow.h"

#include "accountsdialog.h"
#include "blistdialogs.h"
#include "core.h"
#include "debugwindow.h"
#include "pluginsdialog.h"
#include "pounces.h"
#include "prefsdialog.h"
#include "sound.h"
#include "transferswindow.h"
#include "trayicon.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QHeaderView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QScrollBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>

static BuddyListWindow *s_blist = nullptr;
static int s_signalHandle = 0;

/* ================================================================ ui ops */

static void blistNewList(PurpleBuddyList *) {}
static void blistNewNode(PurpleBlistNode *) {}

static void blistShow(PurpleBuddyList *)
{
    BuddyListWindow *w = BuddyListWindow::instance();
    w->show();
    w->scheduleRebuild(0);
}

static void blistUpdate(PurpleBuddyList *, PurpleBlistNode *)
{
    BuddyListWindow::instance()->scheduleRebuild();
}

static void blistRemove(PurpleBuddyList *, PurpleBlistNode *node)
{
    BuddyListWindow::instance()->nodeRemoved(node);
}

static void blistDestroy(PurpleBuddyList *)
{
    BuddyListWindow::instance()->listDestroyed();
}

static void blistSetVisible(PurpleBuddyList *, gboolean show)
{
    BuddyListWindow *w = BuddyListWindow::instance();
    if (show) {
        w->show();
        w->raise();
    } else {
        w->hide();
    }
}

static void blistRequestAddBuddy(PurpleAccount *account, const char *username,
                                 const char *group, const char *alias)
{
    BuddyListWindow::instance()->showAddBuddyDialog(account, qs(username), qs(group), qs(alias));
}

static void blistRequestAddChat(PurpleAccount *account, PurpleGroup *group,
                                const char *alias, const char *name)
{
    BuddyListWindow::instance()->showAddChatDialog(
        account, group ? qs(purple_group_get_name(group)) : QString(), qs(alias), qs(name));
}

static void blistRequestAddGroup()
{
    BuddyListWindow::instance()->showAddGroupDialog();
}

PurpleBlistUiOps *BuddyListWindow::uiOps()
{
    /* Not const: libpurple fills in default save_node/remove_node/save_account. */
    static PurpleBlistUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.new_list = blistNewList;
        ops.new_node = blistNewNode;
        ops.show = blistShow;
        ops.update = blistUpdate;
        ops.remove = blistRemove;
        ops.destroy = blistDestroy;
        ops.set_visible = blistSetVisible;
        ops.request_add_buddy = blistRequestAddBuddy;
        ops.request_add_chat = blistRequestAddChat;
        ops.request_add_group = blistRequestAddGroup;
        ready = true;
    }
    return &ops;
}

/* ============================================================== helpers */

static void savedStatusChanged(PurpleSavedStatus *, PurpleSavedStatus *, gpointer)
{
    BuddyListWindow::instance()->syncStatusBox();
}

static void blistSignalUpdate(gpointer, gpointer)
{
    BuddyListWindow::instance()->scheduleRebuild();
}

static void mutePrefChanged(const char *, PurplePrefType, gconstpointer, gpointer)
{
    BuddyListWindow::instance()->syncMuteAction();
}

static void freeMenuActions(GList *list)
{
    for (GList *l = list; l; l = l->next) {
        PurpleMenuAction *act = static_cast<PurpleMenuAction *>(l->data);
        if (!act)
            continue;
        freeMenuActions(act->children);
        purple_menu_action_free(act);
    }
    g_list_free(list);
}

static void addMenuActions(QMenu *menu, GList *list, PurpleBlistNode *node)
{
    for (GList *l = list; l; l = l->next) {
        PurpleMenuAction *act = static_cast<PurpleMenuAction *>(l->data);
        if (!act) {
            menu->addSeparator();
            continue;
        }
        if (act->children) {
            QMenu *sub = menu->addMenu(QtPurple::mnemonicToQt(act->label));
            addMenuActions(sub, act->children, node);
            continue;
        }
        QAction *qa = menu->addAction(QtPurple::mnemonicToQt(act->label));
        PurpleCallback cb = act->callback;
        gpointer data = act->data;
        if (!cb) {
            qa->setEnabled(false);
            continue;
        }
        QObject::connect(qa, &QAction::triggered, menu, [node, cb, data]() {
            if (!BuddyListWindow::nodeExists(node))
                return;
            reinterpret_cast<void (*)(gpointer, gpointer)>(cb)(node, data);
        });
    }
}

static PurplePluginProtocolInfo *prplInfoFor(PurpleAccount *account, PurpleConnection **gcOut = nullptr)
{
    PurpleConnection *gc = account ? purple_account_get_connection(account) : nullptr;
    if (gcOut)
        *gcOut = gc;
    if (!gc)
        return nullptr;
    PurplePlugin *prpl = purple_connection_get_prpl(gc);
    return prpl ? PURPLE_PLUGIN_PROTOCOL_INFO(prpl) : nullptr;
}

/* ======================================================= BuddyListWindow */

BuddyListWindow *BuddyListWindow::instance()
{
    if (!s_blist)
        s_blist = new BuddyListWindow();
    return s_blist;
}

bool BuddyListWindow::nodeExists(PurpleBlistNode *node)
{
    if (!node || !purple_get_blist())
        return false;
    for (PurpleBlistNode *n = purple_blist_get_root(); n; n = purple_blist_node_next(n, TRUE)) {
        if (n == node)
            return true;
    }
    return false;
}

BuddyListWindow::BuddyListWindow()
    : QMainWindow(nullptr)
{
    setWindowTitle(tr("Buddy List - %1").arg(QStringLiteral(QTPURPLE_NAME)));

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->viewport()->installEventFilter(this);

    connect(m_tree, &QTreeWidget::itemActivated, this, &BuddyListWindow::onItemActivated);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &BuddyListWindow::onContextMenu);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &BuddyListWindow::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, &BuddyListWindow::onItemCollapsed);

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    connect(m_rebuildTimer, &QTimer::timeout, this, &BuddyListWindow::rebuild);

    buildStatusBox();

    QWidget *central = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(central);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->addWidget(m_tree, 1);
    lay->addWidget(m_statusMessage);
    lay->addWidget(m_status);
    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);

    buildMenus();

    purple_signal_connect(purple_savedstatuses_get_handle(), "savedstatus-changed",
                          &s_signalHandle, PURPLE_CALLBACK(savedStatusChanged), nullptr);
    /* Account state changes affect which buddies are shown. */
    void *accounts = purple_accounts_get_handle();
    purple_signal_connect(accounts, "account-signed-on", &s_signalHandle,
                          PURPLE_CALLBACK(blistSignalUpdate), nullptr);
    purple_signal_connect(accounts, "account-signed-off", &s_signalHandle,
                          PURPLE_CALLBACK(blistSignalUpdate), nullptr);
    purple_prefs_connect_callback(&s_signalHandle, QTPURPLE_PREF_SOUND_MUTE, mutePrefChanged, nullptr);

    resize(280, 560);
}

void BuddyListWindow::buildMenus()
{
    /* Buddies */
    QMenu *buddies = menuBar()->addMenu(tr("&Buddies"));
    buddies->addAction(tr("New &Instant Message..."), this, &BuddyListWindow::showNewImDialog,
                       QKeySequence(Qt::CTRL | Qt::Key_M));
    buddies->addAction(tr("Join a &Chat..."), this, &BuddyListWindow::showJoinChatDialog,
                       QKeySequence(Qt::CTRL | Qt::Key_C));
    buddies->addAction(tr("Get User &Info..."), this, &BuddyListWindow::showGetInfoDialog,
                       QKeySequence(Qt::CTRL | Qt::Key_I));
    buddies->addSeparator();

    m_showOffline = buddies->addAction(tr("Show &Offline Buddies"));
    m_showOffline->setCheckable(true);
    connect(m_showOffline, &QAction::toggled, this, [this](bool on) {
        purple_prefs_set_bool(QTPURPLE_PREF_OFFLINE, on ? TRUE : FALSE);
        scheduleRebuild(0);
    });
    m_showEmpty = buddies->addAction(tr("Show &Empty Groups"));
    m_showEmpty->setCheckable(true);
    connect(m_showEmpty, &QAction::toggled, this, [this](bool on) {
        purple_prefs_set_bool(QTPURPLE_PREF_EMPTY, on ? TRUE : FALSE);
        scheduleRebuild(0);
    });
    buddies->addSeparator();

    buddies->addAction(tr("&Add Buddy..."), this, [this]() {
        showAddBuddyDialog(nullptr, QString(), QString(), QString());
    }, QKeySequence(Qt::CTRL | Qt::Key_B));
    buddies->addAction(tr("Add C&hat..."), this, [this]() {
        showAddChatDialog(nullptr, QString(), QString(), QString());
    });
    buddies->addAction(tr("Add &Group..."), this, &BuddyListWindow::showAddGroupDialog);
    buddies->addSeparator();
    buddies->addAction(tr("&Quit"), this, []() { Core::requestQuit(); },
                       QKeySequence(Qt::CTRL | Qt::Key_Q));

    /* Accounts (filled on demand). The "Manage Accounts" action is owned by
     * the window so its shortcut works before the menu was ever opened. */
    m_manageAccounts = new QAction(tr("&Manage Accounts"), this);
    m_manageAccounts->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    connect(m_manageAccounts, &QAction::triggered, this, []() { AccountsDialog::showDialog(); });
    addAction(m_manageAccounts);
    m_accountsMenu = menuBar()->addMenu(tr("&Accounts"));
    m_accountsMenu->addAction(m_manageAccounts);
    connect(m_accountsMenu, &QMenu::aboutToShow, this, &BuddyListWindow::populateAccountsMenu);

    /* Tools */
    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("Buddy &Pounces"), this, []() { Pounces::showManager(); });
    tools->addAction(tr("Plu&gins"), this, []() { PluginsDialog::showDialog(); },
                     QKeySequence(Qt::CTRL | Qt::Key_U));
    tools->addAction(tr("Pr&eferences"), this, []() { PreferencesDialog::showDialog(); },
                     QKeySequence(Qt::CTRL | Qt::Key_P));
    tools->addAction(tr("File &Transfers"), this, []() { TransfersWindow::instance()->present(); },
                     QKeySequence(Qt::CTRL | Qt::Key_T));
    m_mute = tools->addAction(tr("&Mute Sounds"));
    m_mute->setCheckable(true);
    m_mute->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    connect(m_mute, &QAction::toggled, this, [](bool on) { Sound::setMuted(on); });
    tools->addSeparator();
    tools->addAction(tr("&Debug Window"), this, []() {
        DebugWindow *w = DebugWindow::instance();
        w->show();
        w->raise();
        w->activateWindow();
    });

    /* Help */
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, [this]() {
        QMessageBox::about(this, tr("About %1").arg(QStringLiteral(QTPURPLE_NAME)),
                           tr("<b>%1 %2</b><br>A Qt instant messenger built on libpurple %3.<br>"
                              "Qt %4")
                               .arg(QStringLiteral(QTPURPLE_NAME), QStringLiteral(QTPURPLE_VERSION),
                                    qs(purple_core_get_version()), QString::fromLatin1(qVersion())));
    });
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void BuddyListWindow::buildStatusBox()
{
    m_status = new QComboBox(this);
    const struct { PurpleStatusPrimitive prim; const char *label; } entries[] = {
        { PURPLE_STATUS_AVAILABLE,     QT_TR_NOOP("Available") },
        { PURPLE_STATUS_AWAY,          QT_TR_NOOP("Away") },
        { PURPLE_STATUS_UNAVAILABLE,   QT_TR_NOOP("Do not disturb") },
        { PURPLE_STATUS_EXTENDED_AWAY, QT_TR_NOOP("Extended away") },
        { PURPLE_STATUS_INVISIBLE,     QT_TR_NOOP("Invisible") },
        { PURPLE_STATUS_OFFLINE,       QT_TR_NOOP("Offline") },
    };
    for (const auto &e : entries)
        m_status->addItem(QtPurple::statusIcon(e.prim), tr(e.label), int(e.prim));
    connect(m_status, QOverload<int>::of(&QComboBox::activated), this, &BuddyListWindow::onStatusChosen);

    m_statusMessage = new QLineEdit(this);
    m_statusMessage->setPlaceholderText(tr("Status message (press Enter to set)"));
    m_statusMessage->setClearButtonEnabled(true);
    connect(m_statusMessage, &QLineEdit::returnPressed, this, &BuddyListWindow::onStatusMessageEdited);
}

void BuddyListWindow::applyPrefs()
{
    const int w = purple_prefs_get_int(QTPURPLE_PREF_WIDTH);
    const int h = purple_prefs_get_int(QTPURPLE_PREF_HEIGHT);
    if (w > 100 && h > 100)
        resize(w, h);

    const QSignalBlocker b1(m_showOffline);
    const QSignalBlocker b2(m_showEmpty);
    m_showOffline->setChecked(purple_prefs_get_bool(QTPURPLE_PREF_OFFLINE));
    m_showEmpty->setChecked(purple_prefs_get_bool(QTPURPLE_PREF_EMPTY));
    syncMuteAction();
    syncStatusBox();
    scheduleRebuild(0);
}

void BuddyListWindow::saveGeometryPrefs()
{
    purple_prefs_set_int(QTPURPLE_PREF_WIDTH, width());
    purple_prefs_set_int(QTPURPLE_PREF_HEIGHT, height());
}

void BuddyListWindow::showStatusMessage(const QString &message, int timeoutMs)
{
    statusBar()->showMessage(message, timeoutMs);
}

void BuddyListWindow::closeEvent(QCloseEvent *event)
{
    saveGeometryPrefs();
    event->accept();
    /* With a tray icon the buddy list only hides; Quit exits. */
    if (TrayIcon::instance()->isShown())
        return;
    Core::requestQuit();
}

void BuddyListWindow::syncMuteAction()
{
    const QSignalBlocker block(m_mute);
    m_mute->setChecked(Sound::muted());
}

/* ---------------------------------------------------------------- status */

void BuddyListWindow::syncStatusBox()
{
    PurpleSavedStatus *saved = purple_savedstatus_get_current();
    if (!saved)
        return;

    m_syncingStatus = true;
    const int idx = m_status->findData(int(purple_savedstatus_get_type(saved)));
    if (idx >= 0)
        m_status->setCurrentIndex(idx);

    const char *msg = purple_savedstatus_get_message(saved);
    if (msg && *msg) {
        gchar *plain = purple_markup_strip_html(msg);
        m_statusMessage->setText(qs(plain));
        g_free(plain);
    } else {
        m_statusMessage->clear();
    }
    m_syncingStatus = false;
}

void BuddyListWindow::applyStatus()
{
    const PurpleStatusPrimitive prim =
        static_cast<PurpleStatusPrimitive>(m_status->currentData().toInt());
    const QString text = m_statusMessage->text().trimmed();
    QByteArray html = text.toHtmlEscaped().toUtf8();
    const char *message = html.isEmpty() ? nullptr : html.constData();

    PurpleSavedStatus *saved = purple_savedstatus_find_transient_by_type_and_message(prim, message);
    if (!saved) {
        saved = purple_savedstatus_new(nullptr, prim);
        purple_savedstatus_set_message(saved, message);
    }
    purple_savedstatus_activate(saved);
}

void BuddyListWindow::setStatusPrimitive(PurpleStatusPrimitive prim)
{
    const int idx = m_status->findData(int(prim));
    if (idx < 0)
        return;
    m_syncingStatus = true;
    m_status->setCurrentIndex(idx);
    m_syncingStatus = false;
    applyStatus();
}

void BuddyListWindow::onStatusChosen(int)
{
    if (!m_syncingStatus)
        applyStatus();
}

void BuddyListWindow::onStatusMessageEdited()
{
    if (!m_syncingStatus)
        applyStatus();
}

/* --------------------------------------------------------------- rebuild */

void BuddyListWindow::scheduleRebuild(int delayMs)
{
    if (m_dead)
        return;
    if (m_rebuildTimer->isActive() && m_rebuildTimer->remainingTime() <= delayMs)
        return;
    m_rebuildTimer->start(delayMs);
}

void BuddyListWindow::nodeRemoved(PurpleBlistNode *node)
{
    m_expandedContacts.remove(node);
    if (m_dead)
        return;
    /* The node is freed right after this callback returns: make sure no
     * tree item can still refer to it. */
    if (m_items.contains(node)) {
        m_building = true;
        m_tree->clear();
        m_items.clear();
        m_building = false;
    }
    scheduleRebuild(0);
}

void BuddyListWindow::listDestroyed()
{
    m_dead = true;
    m_rebuildTimer->stop();
    m_building = true;
    m_tree->clear();
    m_items.clear();
    m_building = false;
}

PurpleBlistNode *BuddyListWindow::itemNode(const QTreeWidgetItem *item)
{
    if (!item)
        return nullptr;
    return reinterpret_cast<PurpleBlistNode *>(item->data(0, Qt::UserRole).value<quintptr>());
}

BuddyListWindow::ItemKind BuddyListWindow::itemKind(const QTreeWidgetItem *item)
{
    return static_cast<ItemKind>(item ? item->data(0, Qt::UserRole + 1).toInt() : 0);
}

PurpleBlistNode *BuddyListWindow::currentNode() const
{
    return itemNode(m_tree->currentItem());
}

QTreeWidgetItem *BuddyListWindow::makeBuddyItem(PurpleBuddy *buddy, ItemKind kind, PurpleBlistNode *node)
{
    PurplePresence *presence = purple_buddy_get_presence(buddy);
    PurpleStatus *status = purple_presence_get_active_status(presence);
    PurpleStatusPrimitive prim = PURPLE_STATUS_OFFLINE;
    if (PURPLE_BUDDY_IS_ONLINE(buddy) && status)
        prim = purple_status_type_get_primitive(purple_status_get_type(status));
    const bool idle = purple_presence_is_idle(presence);

    QString name;
    if (kind == KindContact)
        name = qs(purple_contact_get_alias(reinterpret_cast<PurpleContact *>(node)));
    else
        name = QStringLiteral("%1 [%2]").arg(qs(purple_buddy_get_alias(buddy)),
                                              qs(purple_account_get_username(purple_buddy_get_account(buddy))));

    /* Status text: ask the protocol first, then fall back to the message. */
    QString statusText;
    PurplePluginProtocolInfo *info = prplInfoFor(purple_buddy_get_account(buddy));
    if (info && info->status_text && PURPLE_BUDDY_IS_ONLINE(buddy)) {
        char *t = info->status_text(buddy);
        if (t) {
            gchar *plain = purple_markup_strip_html(t);
            statusText = qs(plain).simplified();
            g_free(plain);
            g_free(t);
        }
    }

    QString text = name;
    if (!statusText.isEmpty())
        text += QStringLiteral(" \u2014 ") + statusText;
    if (idle) {
        const time_t since = purple_presence_get_idle_time(presence);
        if (since > 0) {
            const long mins = long((time(nullptr) - since) / 60);
            if (mins > 0)
                text += tr(" (idle %1m)").arg(mins);
        }
    }

    QTreeWidgetItem *item = new QTreeWidgetItem();
    item->setText(0, text);
    item->setIcon(0, QtPurple::statusIcon(prim, idle));
    item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(node)));
    item->setData(0, Qt::UserRole + 1, int(kind));
    if (prim == PURPLE_STATUS_OFFLINE || idle)
        item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
    return item;
}

QTreeWidgetItem *BuddyListWindow::makeContactItem(PurpleContact *contact, bool showOffline)
{
    PurpleBlistNode *cnode = PURPLE_BLIST_NODE(contact);
    if (!PURPLE_BLIST_NODE_IS_VISIBLE(cnode))
        return nullptr;

    const bool contactShowOffline = purple_blist_node_get_bool(cnode, "show_offline");
    QList<PurpleBuddy *> shown;
    for (PurpleBlistNode *n = purple_blist_node_get_first_child(cnode); n;
         n = purple_blist_node_get_sibling_next(n)) {
        if (!PURPLE_BLIST_NODE_IS_BUDDY(n) || !PURPLE_BLIST_NODE_IS_VISIBLE(n))
            continue;
        PurpleBuddy *b = PURPLE_BUDDY(n);
        if (!purple_account_is_connected(purple_buddy_get_account(b)))
            continue;
        if (!PURPLE_BUDDY_IS_ONLINE(b) && !showOffline && !contactShowOffline &&
            !purple_blist_node_get_bool(n, "show_offline"))
            continue;
        shown.append(b);
    }
    if (shown.isEmpty())
        return nullptr;

    PurpleBuddy *priority = purple_contact_get_priority_buddy(contact);
    if (!shown.contains(priority))
        priority = shown.first();

    QTreeWidgetItem *item = makeBuddyItem(priority, KindContact, cnode);
    m_items.insert(cnode, item);

    if (shown.size() > 1) {
        for (PurpleBuddy *b : shown) {
            QTreeWidgetItem *child = makeBuddyItem(b, KindBuddy, PURPLE_BLIST_NODE(b));
            item->addChild(child);
            m_items.insert(PURPLE_BLIST_NODE(b), child);
        }
    }
    return item;
}

QTreeWidgetItem *BuddyListWindow::makeChatItem(PurpleChat *chat)
{
    PurpleBlistNode *node = PURPLE_BLIST_NODE(chat);
    QTreeWidgetItem *item = new QTreeWidgetItem();
    item->setText(0, qs(purple_chat_get_name(chat)));
    item->setIcon(0, QtPurple::chatIcon());
    item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(node)));
    item->setData(0, Qt::UserRole + 1, int(KindChat));
    m_items.insert(node, item);
    return item;
}

void BuddyListWindow::rebuild()
{
    if (m_dead || !purple_get_blist())
        return;
    m_rebuildTimer->stop();

    const bool showOffline = purple_prefs_get_bool(QTPURPLE_PREF_OFFLINE);
    const bool showEmpty = purple_prefs_get_bool(QTPURPLE_PREF_EMPTY);
    PurpleBlistNode *selected = currentNode();
    const int scroll = m_tree->verticalScrollBar()->value();

    m_building = true;
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    m_items.clear();

    QFont groupFont = m_tree->font();
    groupFont.setBold(true);

    for (PurpleBlistNode *gnode = purple_blist_get_root(); gnode;
         gnode = purple_blist_node_get_sibling_next(gnode)) {
        if (!PURPLE_BLIST_NODE_IS_GROUP(gnode) || !PURPLE_BLIST_NODE_IS_VISIBLE(gnode))
            continue;
        PurpleGroup *group = PURPLE_GROUP(gnode);

        QList<QTreeWidgetItem *> children;
        for (PurpleBlistNode *n = purple_blist_node_get_first_child(gnode); n;
             n = purple_blist_node_get_sibling_next(n)) {
            if (PURPLE_BLIST_NODE_IS_CONTACT(n)) {
                if (QTreeWidgetItem *it = makeContactItem(PURPLE_CONTACT(n), showOffline))
                    children.append(it);
            } else if (PURPLE_BLIST_NODE_IS_CHAT(n)) {
                PurpleChat *chat = PURPLE_CHAT(n);
                if (PURPLE_BLIST_NODE_IS_VISIBLE(n) &&
                    purple_account_is_connected(purple_chat_get_account(chat)))
                    children.append(makeChatItem(chat));
            }
        }
        if (children.isEmpty() && !showEmpty)
            continue;

        QTreeWidgetItem *gi = new QTreeWidgetItem();
        gi->setText(0, QStringLiteral("%1 (%2/%3)")
                           .arg(qs(purple_group_get_name(group)))
                           .arg(purple_blist_get_group_online_count(group))
                           .arg(purple_blist_get_group_size(group, FALSE)));
        gi->setFont(0, groupFont);
        gi->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(gnode)));
        gi->setData(0, Qt::UserRole + 1, int(KindGroup));
        gi->addChildren(children);
        m_tree->addTopLevelItem(gi);
        m_items.insert(gnode, gi);

        gi->setExpanded(!purple_blist_node_get_bool(gnode, "collapsed"));
        for (QTreeWidgetItem *c : children) {
            if (c->childCount() > 0)
                c->setExpanded(m_expandedContacts.contains(itemNode(c)));
        }
    }

    if (selected) {
        if (QTreeWidgetItem *it = m_items.value(selected, nullptr))
            m_tree->setCurrentItem(it);
    }
    m_tree->verticalScrollBar()->setValue(scroll);
    m_tree->setUpdatesEnabled(true);
    m_building = false;
}

void BuddyListWindow::onItemExpanded(QTreeWidgetItem *item)
{
    if (m_building)
        return;
    PurpleBlistNode *node = itemNode(item);
    if (itemKind(item) == KindGroup)
        purple_blist_node_set_bool(node, "collapsed", FALSE);
    else if (itemKind(item) == KindContact)
        m_expandedContacts.insert(node);
}

void BuddyListWindow::onItemCollapsed(QTreeWidgetItem *item)
{
    if (m_building)
        return;
    PurpleBlistNode *node = itemNode(item);
    if (itemKind(item) == KindGroup)
        purple_blist_node_set_bool(node, "collapsed", TRUE);
    else if (itemKind(item) == KindContact)
        m_expandedContacts.remove(node);
}

/* -------------------------------------------------------------- tooltips */

QString BuddyListWindow::tooltipFor(PurpleBlistNode *node) const
{
    if (!node)
        return QString();

    if (PURPLE_BLIST_NODE_IS_GROUP(node)) {
        PurpleGroup *g = PURPLE_GROUP(node);
        return tr("<b>%1</b><br>%2 of %3 buddies online")
            .arg(qs(purple_group_get_name(g)).toHtmlEscaped())
            .arg(purple_blist_get_group_online_count(g))
            .arg(purple_blist_get_group_size(g, FALSE));
    }

    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
        PurpleChat *chat = PURPLE_CHAT(node);
        return tr("<b>%1</b><br>Account: %2")
            .arg(qs(purple_chat_get_name(chat)).toHtmlEscaped(),
                 QtPurple::accountDisplayName(purple_chat_get_account(chat)).toHtmlEscaped());
    }

    PurpleBuddy *buddy = nullptr;
    if (PURPLE_BLIST_NODE_IS_CONTACT(node))
        buddy = purple_contact_get_priority_buddy(PURPLE_CONTACT(node));
    else if (PURPLE_BLIST_NODE_IS_BUDDY(node))
        buddy = PURPLE_BUDDY(node);
    if (!buddy)
        return QString();

    PurpleAccount *account = purple_buddy_get_account(buddy);
    QString html = QStringLiteral("<b>%1</b><br>").arg(qs(purple_buddy_get_alias(buddy)).toHtmlEscaped());
    html += tr("Username: %1<br>").arg(qs(purple_buddy_get_name(buddy)).toHtmlEscaped());
    html += tr("Account: %1").arg(QtPurple::accountDisplayName(account).toHtmlEscaped());

    PurplePluginProtocolInfo *info = prplInfoFor(account);
    if (info && info->tooltip_text) {
        PurpleNotifyUserInfo *ui = purple_notify_user_info_new();
        info->tooltip_text(buddy, ui, TRUE);
        char *text = purple_notify_user_info_get_text_with_newline(ui, "<br>");
        if (text && *text)
            html += QStringLiteral("<br>") + qs(text);
        g_free(text);
        purple_notify_user_info_destroy(ui);
    } else {
        PurpleStatus *status = purple_presence_get_active_status(purple_buddy_get_presence(buddy));
        if (status)
            html += tr("<br>Status: %1").arg(qs(purple_status_get_name(status)).toHtmlEscaped());
    }
    return html;
}

bool BuddyListWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport() && event->type() == QEvent::ToolTip) {
        QHelpEvent *he = static_cast<QHelpEvent *>(event);
        QTreeWidgetItem *item = m_tree->itemAt(he->pos());
        const QString tip = item ? tooltipFor(itemNode(item)) : QString();
        if (tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(he->globalPos(), tip, m_tree->viewport());
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

/* ------------------------------------------------------------ activation */

void BuddyListWindow::openIm(PurpleBuddy *buddy)
{
    PurpleAccount *account = purple_buddy_get_account(buddy);
    if (!purple_account_is_connected(account))
        return;
    const char *name = purple_buddy_get_name(buddy);
    PurpleConversation *conv =
        purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, name, account);
    if (!conv)
        conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, name);
    if (conv)
        purple_conversation_present(conv);
}

void BuddyListWindow::joinChat(PurpleChat *chat)
{
    PurpleAccount *account = purple_chat_get_account(chat);
    PurpleConnection *gc = nullptr;
    PurplePluginProtocolInfo *info = prplInfoFor(account, &gc);
    if (!gc)
        return;

    GHashTable *components = purple_chat_get_components(chat);
    PurpleConversation *conv = nullptr;
    if (info && info->get_chat_name) {
        char *name = info->get_chat_name(components);
        if (name)
            conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, name, account);
        g_free(name);
    }
    if (conv) {
        purple_conversation_present(conv);
        if (!purple_conv_chat_has_left(PURPLE_CONV_CHAT(conv)))
            return;
    }
    serv_join_chat(gc, components);
}

void BuddyListWindow::onItemActivated(QTreeWidgetItem *item, int)
{
    PurpleBlistNode *node = itemNode(item);
    if (!node)
        return;
    switch (itemKind(item)) {
    case KindContact: {
        PurpleBuddy *b = purple_contact_get_priority_buddy(PURPLE_CONTACT(node));
        if (b)
            openIm(b);
        break;
    }
    case KindBuddy:
        openIm(PURPLE_BUDDY(node));
        break;
    case KindChat:
        joinChat(PURPLE_CHAT(node));
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------- context menu */

void BuddyListWindow::appendProtocolMenu(QMenu *menu, PurpleBlistNode *node, PurpleAccount *account)
{
    GList *actions = nullptr;
    PurplePluginProtocolInfo *info = prplInfoFor(account);
    if (info && info->blist_node_menu)
        actions = info->blist_node_menu(node);
    GList *extended = purple_blist_node_get_extended_menu(node);

    if (actions || extended)
        menu->addSeparator();
    addMenuActions(menu, actions, node);
    if (actions && extended)
        menu->addSeparator();
    addMenuActions(menu, extended, node);

    freeMenuActions(actions);
    freeMenuActions(extended);
}

static void addMoveMenu(QMenu *menu, PurpleBlistNode *node, PurpleGroup *current)
{
    QMenu *move = menu->addMenu(QObject::tr("&Move to"));
    for (PurpleBlistNode *g = purple_blist_get_root(); g; g = purple_blist_node_get_sibling_next(g)) {
        if (!PURPLE_BLIST_NODE_IS_GROUP(g))
            continue;
        PurpleGroup *group = PURPLE_GROUP(g);
        QAction *a = move->addAction(qs(purple_group_get_name(group)));
        if (group == current) {
            a->setEnabled(false);
            continue;
        }
        QObject::connect(a, &QAction::triggered, menu, [node, g]() {
            if (!BuddyListWindow::nodeExists(node) || !BuddyListWindow::nodeExists(g))
                return;
            if (PURPLE_BLIST_NODE_IS_CONTACT(node))
                purple_blist_add_contact(PURPLE_CONTACT(node), PURPLE_GROUP(g), nullptr);
            else if (PURPLE_BLIST_NODE_IS_BUDDY(node))
                purple_blist_add_buddy(PURPLE_BUDDY(node), nullptr, PURPLE_GROUP(g), nullptr);
            else if (PURPLE_BLIST_NODE_IS_CHAT(node))
                purple_blist_add_chat(PURPLE_CHAT(node), PURPLE_GROUP(g), nullptr);
        });
    }
}

void BuddyListWindow::buddyMenu(QMenu *menu, PurpleBuddy *buddy, PurpleContact *contact)
{
    PurpleBlistNode *bnode = PURPLE_BLIST_NODE(buddy);
    PurpleBlistNode *cnode = contact ? PURPLE_BLIST_NODE(contact) : nullptr;
    PurpleAccount *account = purple_buddy_get_account(buddy);
    PurpleConnection *gc = nullptr;
    PurplePluginProtocolInfo *info = prplInfoFor(account, &gc);
    const QByteArray name(purple_buddy_get_name(buddy));

    menu->addAction(tr("Send &IM"), this, [this, bnode]() {
        if (nodeExists(bnode))
            openIm(PURPLE_BUDDY(bnode));
    });

    if (info && info->send_file && (!info->can_receive_file || info->can_receive_file(gc, name.constData()))) {
        menu->addAction(tr("Send &File..."), this, [gc, bnode, name]() {
            if (QtPurple::connectionExists(gc) && nodeExists(bnode))
                serv_send_file(gc, name.constData(), nullptr);
        });
    }

    if (info && info->get_info) {
        menu->addAction(tr("Get &Info"), this, [gc, name]() {
            if (QtPurple::connectionExists(gc))
                serv_get_info(gc, name.constData());
        });
    }

    menu->addAction(tr("Add Buddy &Pounce..."), this, [account, name]() {
        if (QtPurple::accountExists(account))
            Pounces::editPounce(nullptr, account, QString::fromUtf8(name));
    });

    menu->addSeparator();

    /* Alias the contact when it groups several buddies, otherwise the buddy. */
    int buddyCount = 0;
    if (cnode) {
        for (PurpleBlistNode *n = purple_blist_node_get_first_child(cnode); n;
             n = purple_blist_node_get_sibling_next(n))
            if (PURPLE_BLIST_NODE_IS_BUDDY(n))
                ++buddyCount;
    }
    const bool aliasContact = cnode && buddyCount > 1;

    menu->addAction(tr("&Alias..."), this, [this, bnode, cnode, aliasContact]() {
        if (!nodeExists(bnode))
            return;
        const QString current = aliasContact
            ? qs(purple_contact_get_alias(PURPLE_CONTACT(cnode)))
            : qs(purple_buddy_get_alias(PURPLE_BUDDY(bnode)));
        bool ok = false;
        const QString alias = QInputDialog::getText(this, tr("Alias"), tr("Enter an alias:"),
                                                    QLineEdit::Normal, current, &ok).trimmed();
        if (!ok || !nodeExists(bnode))
            return;
        Utf8 a(alias);
        if (aliasContact && nodeExists(cnode)) {
            purple_blist_alias_contact(PURPLE_CONTACT(cnode), a.orNull());
        } else {
            purple_blist_alias_buddy(PURPLE_BUDDY(bnode), a.orNull());
            serv_alias_buddy(PURPLE_BUDDY(bnode));
        }
    });

    PurpleBlistNode *moveNode = cnode ? cnode : bnode;
    addMoveMenu(menu, moveNode, purple_buddy_get_group(buddy));

    QAction *showOff = menu->addAction(tr("Show When &Offline"));
    showOff->setCheckable(true);
    showOff->setChecked(purple_blist_node_get_bool(moveNode, "show_offline"));
    connect(showOff, &QAction::toggled, this, [this, moveNode](bool on) {
        if (!nodeExists(moveNode))
            return;
        purple_blist_node_set_bool(moveNode, "show_offline", on ? TRUE : FALSE);
        scheduleRebuild(0);
    });

    menu->addAction(tr("&Remove"), this, [this, bnode, cnode]() {
        if (cnode) {
            if (nodeExists(cnode))
                removeContact(PURPLE_CONTACT(cnode));
        } else if (nodeExists(bnode)) {
            removeBuddy(PURPLE_BUDDY(bnode));
        }
    });

    appendProtocolMenu(menu, bnode, account);
}

void BuddyListWindow::groupMenu(QMenu *menu, PurpleGroup *group)
{
    PurpleBlistNode *gnode = PURPLE_BLIST_NODE(group);
    const QString name = qs(purple_group_get_name(group));

    menu->addAction(tr("Add &Buddy..."), this, [this, name]() {
        showAddBuddyDialog(nullptr, QString(), name, QString());
    });
    menu->addAction(tr("Add C&hat..."), this, [this, name]() {
        showAddChatDialog(nullptr, name, QString(), QString());
    });
    menu->addSeparator();
    menu->addAction(tr("Re&name..."), this, [this, gnode, name]() {
        bool ok = false;
        const QString newName = QInputDialog::getText(this, tr("Rename Group"), tr("New group name:"),
                                                      QLineEdit::Normal, name, &ok).trimmed();
        if (!ok || newName.isEmpty() || !nodeExists(gnode))
            return;
        purple_blist_rename_group(PURPLE_GROUP(gnode), Utf8(newName));
    });
    menu->addAction(tr("&Remove..."), this, [this, gnode]() {
        if (nodeExists(gnode))
            removeGroup(PURPLE_GROUP(gnode));
    });

    GList *extended = purple_blist_node_get_extended_menu(gnode);
    if (extended) {
        menu->addSeparator();
        addMenuActions(menu, extended, gnode);
        freeMenuActions(extended);
    }
}

void BuddyListWindow::chatMenu(QMenu *menu, PurpleChat *chat)
{
    PurpleBlistNode *node = PURPLE_BLIST_NODE(chat);

    menu->addAction(tr("&Join"), this, [this, node]() {
        if (nodeExists(node))
            joinChat(PURPLE_CHAT(node));
    });
    menu->addSeparator();
    menu->addAction(tr("&Alias..."), this, [this, node]() {
        if (!nodeExists(node))
            return;
        bool ok = false;
        const QString alias = QInputDialog::getText(this, tr("Alias Chat"), tr("Enter an alias:"),
                                                    QLineEdit::Normal,
                                                    qs(purple_chat_get_name(PURPLE_CHAT(node))),
                                                    &ok).trimmed();
        if (ok && nodeExists(node))
            purple_blist_alias_chat(PURPLE_CHAT(node), Utf8(alias).orNull());
    });
    addMoveMenu(menu, node, purple_chat_get_group(chat));
    menu->addAction(tr("&Remove"), this, [this, node]() {
        if (nodeExists(node))
            removeChat(PURPLE_CHAT(node));
    });

    appendProtocolMenu(menu, node, purple_chat_get_account(chat));
}

void BuddyListWindow::onContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    QMenu menu(this);
    PurpleBlistNode *node = itemNode(item);

    if (!item || !node) {
        menu.addAction(tr("Add &Buddy..."), this, [this]() {
            showAddBuddyDialog(nullptr, QString(), QString(), QString());
        });
        menu.addAction(tr("Add C&hat..."), this, [this]() {
            showAddChatDialog(nullptr, QString(), QString(), QString());
        });
        menu.addAction(tr("Add &Group..."), this, &BuddyListWindow::showAddGroupDialog);
    } else {
        switch (itemKind(item)) {
        case KindGroup:
            groupMenu(&menu, PURPLE_GROUP(node));
            break;
        case KindContact: {
            PurpleBuddy *b = purple_contact_get_priority_buddy(PURPLE_CONTACT(node));
            if (!b)
                return;
            buddyMenu(&menu, b, PURPLE_CONTACT(node));
            break;
        }
        case KindBuddy:
            buddyMenu(&menu, PURPLE_BUDDY(node), nullptr);
            break;
        case KindChat:
            chatMenu(&menu, PURPLE_CHAT(node));
            break;
        }
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

/* --------------------------------------------------------------- removal */

static void removeBuddyFromServer(PurpleBuddy *buddy)
{
    PurpleAccount *account = purple_buddy_get_account(buddy);
    if (purple_account_is_connected(account))
        purple_account_remove_buddy(account, buddy, purple_buddy_get_group(buddy));
}

void BuddyListWindow::removeContact(PurpleContact *contact)
{
    PurpleBlistNode *cnode = PURPLE_BLIST_NODE(contact);
    const QString name = qs(purple_contact_get_alias(contact));
    if (QMessageBox::question(this, tr("Remove Contact"),
                              tr("Remove %1 from your buddy list?").arg(name))
        != QMessageBox::Yes)
        return;
    if (!nodeExists(cnode))
        return;

    for (PurpleBlistNode *n = purple_blist_node_get_first_child(cnode); n;
         n = purple_blist_node_get_sibling_next(n)) {
        if (PURPLE_BLIST_NODE_IS_BUDDY(n))
            removeBuddyFromServer(PURPLE_BUDDY(n));
    }
    purple_blist_remove_contact(contact);
}

void BuddyListWindow::removeBuddy(PurpleBuddy *buddy)
{
    PurpleBlistNode *bnode = PURPLE_BLIST_NODE(buddy);
    if (QMessageBox::question(this, tr("Remove Buddy"),
                              tr("Remove %1 from your buddy list?").arg(qs(purple_buddy_get_alias(buddy))))
        != QMessageBox::Yes)
        return;
    if (!nodeExists(bnode))
        return;
    removeBuddyFromServer(buddy);
    purple_blist_remove_buddy(buddy);
}

void BuddyListWindow::removeGroup(PurpleGroup *group)
{
    PurpleBlistNode *gnode = PURPLE_BLIST_NODE(group);
    if (QMessageBox::question(this, tr("Remove Group"),
                              tr("Remove the group %1 and all its buddies and chats?")
                                  .arg(qs(purple_group_get_name(group))))
        != QMessageBox::Yes)
        return;
    if (!nodeExists(gnode))
        return;

    PurpleBlistNode *next = nullptr;
    for (PurpleBlistNode *n = purple_blist_node_get_first_child(gnode); n; n = next) {
        next = purple_blist_node_get_sibling_next(n);
        if (PURPLE_BLIST_NODE_IS_CONTACT(n)) {
            for (PurpleBlistNode *b = purple_blist_node_get_first_child(n); b;
                 b = purple_blist_node_get_sibling_next(b)) {
                if (PURPLE_BLIST_NODE_IS_BUDDY(b))
                    removeBuddyFromServer(PURPLE_BUDDY(b));
            }
            purple_blist_remove_contact(PURPLE_CONTACT(n));
        } else if (PURPLE_BLIST_NODE_IS_CHAT(n)) {
            purple_blist_remove_chat(PURPLE_CHAT(n));
        }
    }

    for (GList *l = purple_connections_get_all(); l; l = l->next) {
        PurpleAccount *account = purple_connection_get_account(static_cast<PurpleConnection *>(l->data));
        if (purple_account_is_connected(account))
            purple_account_remove_group(account, group);
    }
    purple_blist_remove_group(group);
}

void BuddyListWindow::removeChat(PurpleChat *chat)
{
    PurpleBlistNode *node = PURPLE_BLIST_NODE(chat);
    if (QMessageBox::question(this, tr("Remove Chat"),
                              tr("Remove the chat %1 from your buddy list?")
                                  .arg(qs(purple_chat_get_name(chat))))
        != QMessageBox::Yes)
        return;
    if (nodeExists(node))
        purple_blist_remove_chat(chat);
}

/* -------------------------------------------------------------- dialogs */

void BuddyListWindow::showAddBuddyDialog(PurpleAccount *account, const QString &user,
                                         const QString &group, const QString &alias)
{
    AddBuddyDialog *dlg = new AddBuddyDialog(account, user, group, alias, nullptr);
    dlg->show();
    dlg->raise();
}

void BuddyListWindow::showAddChatDialog(PurpleAccount *account, const QString &group,
                                        const QString &alias, const QString &name)
{
    ChatDialog *dlg = new ChatDialog(ChatDialog::AddChat, account, group, alias, name, nullptr);
    dlg->show();
    dlg->raise();
}

void BuddyListWindow::showJoinChatDialog()
{
    ChatDialog *dlg = new ChatDialog(ChatDialog::JoinChat, nullptr, QString(), QString(), QString(), nullptr);
    dlg->show();
    dlg->raise();
}

void BuddyListWindow::showAddGroupDialog()
{
    /* Non-modal: this can be called from inside a libpurple callback. */
    QInputDialog *dlg = new QInputDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Add Group"));
    dlg->setLabelText(tr("Group name:"));
    connect(dlg, &QInputDialog::textValueSelected, this, [](const QString &value) {
        const QString name = value.trimmed();
        if (name.isEmpty())
            return;
        Utf8 n(name);
        if (purple_find_group(n))
            return;
        PurpleGroup *group = purple_group_new(n);
        purple_blist_add_group(group, nullptr);
    });
    dlg->show();
}

void BuddyListWindow::showNewImDialog()
{
    AccountNameDialog *dlg = new AccountNameDialog(
        tr("New Instant Message"), tr("&Name:"),
        [](PurpleAccount *account, const QString &name) {
            Utf8 n(name);
            PurpleConversation *conv =
                purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, n, account);
            if (!conv)
                conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, n);
            if (conv)
                purple_conversation_present(conv);
        });
    dlg->show();
}

void BuddyListWindow::showGetInfoDialog()
{
    AccountNameDialog *dlg = new AccountNameDialog(
        tr("Get User Info"), tr("&Name:"),
        [](PurpleAccount *account, const QString &name) {
            PurpleConnection *gc = purple_account_get_connection(account);
            if (gc)
                serv_get_info(gc, Utf8(name));
        });
    dlg->show();
}

/* --------------------------------------------------------- accounts menu */

void BuddyListWindow::freePluginActions()
{
    for (PurplePluginAction *a : m_pluginActions)
        purple_plugin_action_free(a);
    m_pluginActions.clear();
}

void BuddyListWindow::populateAccountsMenu()
{
    freePluginActions();
    m_accountsMenu->clear();
    for (QMenu *m : m_accountSubmenus)
        m->deleteLater();
    m_accountSubmenus.clear();

    m_accountsMenu->addAction(m_manageAccounts);

    QMenu *enable = new QMenu(tr("&Enable Account"), m_accountsMenu);
    m_accountSubmenus.append(enable);
    m_accountsMenu->addMenu(enable);
    bool anyDisabled = false;
    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        PurpleAccount *account = static_cast<PurpleAccount *>(l->data);
        if (purple_account_get_enabled(account, QTPURPLE_UI_ID))
            continue;
        anyDisabled = true;
        enable->addAction(QtPurple::protocolIcon(account), QtPurple::accountDisplayName(account),
                          this, [account]() {
            if (QtPurple::accountExists(account))
                purple_account_set_enabled(account, QTPURPLE_UI_ID, TRUE);
        });
    }
    enable->setEnabled(anyDisabled);

    m_accountsMenu->addSeparator();

    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        PurpleAccount *account = static_cast<PurpleAccount *>(l->data);
        if (!purple_account_get_enabled(account, QTPURPLE_UI_ID))
            continue;

        QMenu *sub = new QMenu(QtPurple::accountDisplayName(account), m_accountsMenu);
        sub->setIcon(QtPurple::statusIcon(QtPurple::accountPrimitive(account)));
        m_accountSubmenus.append(sub);
        m_accountsMenu->addMenu(sub);

        sub->addAction(tr("&Edit Account"), this, [account]() {
            if (QtPurple::accountExists(account))
                AccountsDialog::editAccount(account);
        });

        PurpleConnection *gc = purple_account_get_connection(account);
        if (gc && purple_account_is_connected(account)) {
            PurplePlugin *plugin = purple_connection_get_prpl(gc);
            if (plugin && PURPLE_PLUGIN_HAS_ACTIONS(plugin)) {
                GList *actions = PURPLE_PLUGIN_ACTIONS(plugin, gc);
                if (actions)
                    sub->addSeparator();
                for (GList *a = actions; a; a = a->next) {
                    PurplePluginAction *act = static_cast<PurplePluginAction *>(a->data);
                    if (!act) {
                        sub->addSeparator();
                        continue;
                    }
                    act->plugin = plugin;
                    act->context = gc;
                    m_pluginActions.append(act);
                    sub->addAction(QtPurple::mnemonicToQt(act->label), this, [act, gc]() {
                        if (QtPurple::connectionExists(gc) && act->callback)
                            act->callback(act);
                    });
                }
                g_list_free(actions);
            }
        } else if (!gc) {
            sub->addAction(tr("&Connect"), this, [account]() {
                if (QtPurple::accountExists(account))
                    Core::reconnectNow(account);
            });
        }

        sub->addSeparator();
        sub->addAction(tr("&Disable"), this, [account]() {
            if (QtPurple::accountExists(account))
                purple_account_set_enabled(account, QTPURPLE_UI_ID, FALSE);
        });
    }
}
