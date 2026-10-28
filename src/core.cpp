/*
 * QtPurple - a Qt front end for libpurple
 *
 * libpurple core start-up / shutdown, core, account and connection UI ops.
 */
#include "core.h"

#include "accountsdialog.h"
#include "blistwindow.h"
#include "conversationwindow.h"
#include "debugwindow.h"
#include "emoji.h"
#include "eventloop.h"
#include "idle.h"
#include "notifyui.h"
#include "pounces.h"
#include "privacydialog.h"
#include "requestui.h"
#include "roomlistdialog.h"
#include "sound.h"
#include "transferswindow.h"
#include "trayicon.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

#include <cstring>

namespace {

bool g_shuttingDown = false;
int g_signalHandle = 0;       /* address used as libpurple signal handle */

/* ======================================================= reconnection */

const int kReconnectInitialMs = 8000;
const int kReconnectMaxMs = 4 * 60 * 1000;

struct ReconnectEntry {
    QTimer *timer = nullptr;
    int delay = 0;
};

QHash<PurpleAccount *, ReconnectEntry> g_reconnects;
QHash<PurpleAccount *, QPointer<QMessageBox>> g_errorBoxes;

void cancelReconnect(PurpleAccount *account)
{
    auto it = g_reconnects.find(account);
    if (it == g_reconnects.end())
        return;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    g_reconnects.erase(it);
}

void doReconnect(PurpleAccount *account)
{
    if (!QtPurple::accountExists(account)) {
        cancelReconnect(account);
        return;
    }
    if (!purple_account_get_enabled(account, QTPURPLE_UI_ID)) {
        cancelReconnect(account);
        return;
    }
    PurpleSavedStatus *saved = purple_savedstatus_get_current();
    if (saved && purple_savedstatus_get_type(saved) == PURPLE_STATUS_OFFLINE) {
        cancelReconnect(account);
        return;
    }
    if (!purple_account_is_disconnected(account))
        return;     /* already connecting or connected */

    purple_account_connect(account);
}

void scheduleReconnect(PurpleAccount *account)
{
    ReconnectEntry &e = g_reconnects[account];
    if (!e.timer) {
        e.timer = new QTimer();
        e.timer->setSingleShot(true);
        QObject::connect(e.timer, &QTimer::timeout, e.timer, [account]() { doReconnect(account); });
        e.delay = kReconnectInitialMs;
    } else {
        e.delay = qMin(e.delay * 2, kReconnectMaxMs);
    }
    /* A little jitter so several accounts don't hammer the network at once. */
    const int jitter = int(QRandomGenerator::global()->bounded(e.delay / 4 + 1));
    e.timer->start(e.delay + jitter);

    BuddyListWindow::instance()->showStatusMessage(
        QObject::tr("%1: reconnecting in %2 seconds")
            .arg(QtPurple::accountDisplayName(account))
            .arg((e.delay + jitter) / 1000));
}

/* ======================================================= core ui ops */

void uiPrefsInit()
{
    purple_prefs_add_none(QTPURPLE_PREFS_ROOT);
    purple_prefs_add_none(QTPURPLE_PREF_PLUGINS);
    purple_prefs_add_path_list(QTPURPLE_PREF_LOADED, nullptr);
    purple_prefs_add_none(QTPURPLE_PREF_BLIST);
    purple_prefs_add_bool(QTPURPLE_PREF_OFFLINE, FALSE);
    purple_prefs_add_bool(QTPURPLE_PREF_EMPTY, FALSE);
    purple_prefs_add_int(QTPURPLE_PREF_WIDTH, 280);
    purple_prefs_add_int(QTPURPLE_PREF_HEIGHT, 560);
    purple_prefs_add_none(QTPURPLE_PREF_CONV);
    purple_prefs_add_int(QTPURPLE_PREF_CONV_W, 680);
    purple_prefs_add_int(QTPURPLE_PREF_CONV_H, 480);
    purple_prefs_add_none(QTPURPLE_PREF_TRAY);
    purple_prefs_add_bool(QTPURPLE_PREF_TRAY_ENABLED, TRUE);
    purple_prefs_add_bool(QTPURPLE_PREF_TRAY_BLINK, TRUE);
    purple_prefs_add_bool(QTPURPLE_PREF_SHOW_TOOLBAR, TRUE);
    purple_prefs_add_bool(QTPURPLE_PREF_TIMESTAMPS, TRUE);
    purple_prefs_add_string(QTPURPLE_PREF_FONT_FACE, "");
    purple_prefs_add_string(QTPURPLE_PREF_FONT_COLOR, "");
    purple_prefs_add_int(QTPURPLE_PREF_FONT_SIZE, 3);
    Sound::addPrefs();
    Emoji::addPrefs();
}

void debugUiInit()
{
    purple_debug_set_ui_ops(DebugWindow::uiOps());
}

void onAccountGone(PurpleAccount *account, gpointer)
{
    cancelReconnect(account);
    auto it = g_errorBoxes.find(account);
    if (it != g_errorBoxes.end()) {
        if (it.value())
            it.value()->deleteLater();
        g_errorBoxes.erase(it);
    }
}

PurpleAccountUiOps *accountUiOps();
PurpleConnectionUiOps *connectionUiOps();

void uiInit()
{
    purple_accounts_set_ui_ops(accountUiOps());
    purple_connections_set_ui_ops(connectionUiOps());
    purple_blist_set_ui_ops(BuddyListWindow::uiOps());
    purple_conversations_set_ui_ops(ConversationWindow::uiOps());
    purple_request_set_ui_ops(RequestUi::uiOps());
    purple_notify_set_ui_ops(NotifyUi::uiOps());
    purple_xfers_set_ui_ops(TransfersWindow::uiOps());
    purple_roomlist_set_ui_ops(RoomListDialog::uiOps());
    purple_privacy_set_ui_ops(Privacy::uiOps());
    purple_sound_set_ui_ops(Sound::uiOps());     /* runs Sound's init: event hooks */
    purple_idle_set_ui_ops(Idle::uiOps());
    Idle::init();
    Pounces::init();                             /* before purple_pounces_load() */

    void *accounts = purple_accounts_get_handle();
    purple_signal_connect(accounts, "account-disabled", &g_signalHandle,
                          PURPLE_CALLBACK(onAccountGone), nullptr);
    purple_signal_connect(accounts, "account-removed", &g_signalHandle,
                          PURPLE_CALLBACK(onAccountGone), nullptr);

    BuddyListWindow::instance();          /* create the main window */
    ConversationWindow::init();           /* conversation signal hooks */
}

void coreQuit()
{
    /* Nothing to free: windows are hidden in Core::shutdown(). */
}

GHashTable *getUiInfo()
{
    static GHashTable *info = nullptr;
    if (!info) {
        info = g_hash_table_new(g_str_hash, g_str_equal);
        g_hash_table_insert(info, (gpointer)"name", (gpointer)QTPURPLE_NAME);
        g_hash_table_insert(info, (gpointer)"version", (gpointer)QTPURPLE_VERSION);
        g_hash_table_insert(info, (gpointer)"website", (gpointer)QTPURPLE_WEBSITE);
        g_hash_table_insert(info, (gpointer)"dev_website", (gpointer)QTPURPLE_WEBSITE);
        g_hash_table_insert(info, (gpointer)"client_type", (gpointer)"pc");
    }
    return info;
}

PurpleCoreUiOps *coreUiOps()
{
    static PurpleCoreUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.ui_prefs_init = uiPrefsInit;
        ops.debug_ui_init = debugUiInit;
        ops.ui_init = uiInit;
        ops.quit = coreQuit;
        ops.get_ui_info = getUiInfo;
        ready = true;
    }
    return &ops;
}

/* ======================================================= account ui ops */

void notifyAdded(PurpleAccount *account, const char *remote_user, const char *id,
                 const char *alias, const char *message)
{
    const QString who = alias && *alias
        ? QStringLiteral("%1 (%2)").arg(qs(alias), qs(remote_user))
        : qs(remote_user);
    const QString me = qs(id && *id ? id : purple_account_get_username(account));
    const QString primary = QObject::tr("%1 has made %2 their buddy.").arg(who, me);
    purple_notify_info(account, QObject::tr("Buddy added").toUtf8().constData(),
                       primary.toUtf8().constData(), message);
}

struct AddRequest {
    PurpleAccount *account;
    QString user;
    QString alias;
};

void addRequestAdd(AddRequest *req, int)
{
    if (QtPurple::accountExists(req->account))
        BuddyListWindow::instance()->showAddBuddyDialog(req->account, req->user, QString(), req->alias);
    delete req;
}

void addRequestCancel(AddRequest *req, int)
{
    delete req;
}

void requestAdd(PurpleAccount *account, const char *remote_user, const char *id,
                const char *alias, const char *message)
{
    AddRequest *req = new AddRequest{account, qs(remote_user), qs(alias)};
    const QString who = alias && *alias
        ? QStringLiteral("%1 (%2)").arg(qs(alias), qs(remote_user))
        : qs(remote_user);
    const QString me = qs(id && *id ? id : purple_account_get_username(account));
    const QString primary = QObject::tr("%1 has made %2 their buddy. "
                                        "Add them to your buddy list?").arg(who, me);
    purple_request_action(account, QObject::tr("Add buddy").toUtf8().constData(),
                          primary.toUtf8().constData(), message, 0,
                          account, remote_user, nullptr, req, 2,
                          "_Add", G_CALLBACK(addRequestAdd),
                          "_Cancel", G_CALLBACK(addRequestCancel));
}

QSet<QMessageBox *> g_authBoxes;

void *requestAuthorize(PurpleAccount *account, const char *remote_user, const char *id,
                       const char *alias, const char *message, gboolean on_list,
                       PurpleAccountRequestAuthorizationCb authorize_cb,
                       PurpleAccountRequestAuthorizationCb deny_cb, void *user_data)
{
    const QString who = alias && *alias
        ? QStringLiteral("%1 (%2)").arg(qs(alias), qs(remote_user))
        : qs(remote_user);
    const QString me = qs(id && *id ? id : purple_account_get_username(account));

    QMessageBox *box = new QMessageBox(QMessageBox::Question,
                                       QObject::tr("Authorize buddy?"),
                                       QObject::tr("%1 wants to add %2 to their buddy list.")
                                           .arg(who.toHtmlEscaped(), me.toHtmlEscaped()));
    box->setTextFormat(Qt::RichText);
    if (message && *message)
        box->setInformativeText(QObject::tr("Message: %1").arg(qs(message).toHtmlEscaped()));
    box->setModal(false);
    box->setAttribute(Qt::WA_DeleteOnClose, false);

    QPushButton *authBtn = box->addButton(QObject::tr("&Authorize"), QMessageBox::AcceptRole);
    QPushButton *denyBtn = box->addButton(QObject::tr("&Deny"), QMessageBox::RejectRole);
    box->setDefaultButton(authBtn);
    box->setEscapeButton(denyBtn);

    QCheckBox *addBox = nullptr;
    if (!on_list) {
        addBox = new QCheckBox(QObject::tr("Add to my buddy list"));
        addBox->setChecked(true);
        box->setCheckBox(addBox);
    }

    const QString user = qs(remote_user);
    const QString aliasStr = qs(alias);
    g_authBoxes.insert(box);

    QObject::connect(box, &QMessageBox::finished, box,
                     [box, authBtn, addBox, account, user, aliasStr,
                      authorize_cb, deny_cb, user_data](int) {
        if (!g_authBoxes.remove(box))
            return;             /* already closed by libpurple */
        const bool authorized = box->clickedButton() == authBtn;
        const bool wantAdd = authorized && addBox && addBox->isChecked();
        box->deleteLater();

        /* libpurple frees its request record inside these callbacks. */
        if (authorized) {
            if (authorize_cb)
                authorize_cb(user_data);
        } else {
            if (deny_cb)
                deny_cb(user_data);
        }
        if (wantAdd && QtPurple::accountExists(account))
            BuddyListWindow::instance()->showAddBuddyDialog(account, user, QString(), aliasStr);
    });

    box->show();
    QApplication::alert(box);
    return box;
}

void closeAccountRequest(void *ui_handle)
{
    QMessageBox *box = static_cast<QMessageBox *>(ui_handle);
    if (g_authBoxes.remove(box)) {
        box->hide();
        box->deleteLater();
    }
}

PurpleAccountUiOps *accountUiOps()
{
    static PurpleAccountUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.notify_added = notifyAdded;
        ops.request_add = requestAdd;
        ops.request_authorize = requestAuthorize;
        ops.close_account_request = closeAccountRequest;
        ready = true;
    }
    return &ops;
}

/* ===================================================== connection ui ops */

void connectProgress(PurpleConnection *gc, const char *text, size_t step, size_t step_count)
{
    PurpleAccount *account = purple_connection_get_account(gc);
    QString msg = QStringLiteral("%1: %2").arg(QtPurple::accountDisplayName(account), qs(text));
    if (step_count > 0)
        msg += QStringLiteral(" (%1/%2)").arg(qulonglong(step) + 1).arg(qulonglong(step_count));
    BuddyListWindow::instance()->showStatusMessage(msg);
}

void connected(PurpleConnection *gc)
{
    PurpleAccount *account = purple_connection_get_account(gc);
    cancelReconnect(account);
    auto it = g_errorBoxes.find(account);
    if (it != g_errorBoxes.end()) {
        if (it.value())
            it.value()->deleteLater();
        g_errorBoxes.erase(it);
    }
    BuddyListWindow::instance()->showStatusMessage(
        QObject::tr("%1: connected").arg(QtPurple::accountDisplayName(account)), 5000);
}

void disconnected(PurpleConnection *)
{
}

void showFatalError(PurpleAccount *account, const QString &text)
{
    QPointer<QMessageBox> &slot = g_errorBoxes[account];
    const QString title = QObject::tr("%1 disconnected").arg(QtPurple::accountDisplayName(account));

    if (slot) {
        slot->setText(title);
        slot->setInformativeText(text);
        slot->show();
        slot->raise();
        return;
    }

    QMessageBox *box = new QMessageBox(QMessageBox::Warning, QObject::tr("Connection error"), title);
    box->setInformativeText(text);
    box->setModal(false);
    box->setAttribute(Qt::WA_DeleteOnClose, true);
    QPushButton *reconnect = box->addButton(QObject::tr("&Reconnect"), QMessageBox::AcceptRole);
    QPushButton *modify = box->addButton(QObject::tr("&Modify Account"), QMessageBox::ActionRole);
    box->addButton(QMessageBox::Close);
    slot = box;

    QObject::connect(box, &QMessageBox::buttonClicked, box,
                     [account, reconnect, modify](QAbstractButton *b) {
        g_errorBoxes.remove(account);
        if (!QtPurple::accountExists(account))
            return;
        if (b == reconnect) {
            if (!purple_account_get_enabled(account, QTPURPLE_UI_ID))
                purple_account_set_enabled(account, QTPURPLE_UI_ID, TRUE);
            Core::reconnectNow(account);
        } else if (b == modify) {
            AccountsDialog::editAccount(account);
        }
    });
    box->show();
}

void reportDisconnectReason(PurpleConnection *gc, PurpleConnectionError reason, const char *text)
{
    PurpleAccount *account = purple_connection_get_account(gc);
    const QString msg = qs(text);

    BuddyListWindow::instance()->showStatusMessage(
        QStringLiteral("%1: %2").arg(QtPurple::accountDisplayName(account), msg));

    if (g_shuttingDown)
        return;

    if (!purple_connection_error_is_fatal(reason))
        scheduleReconnect(account);
    else
        showFatalError(account, msg);
}

void networkConnected()
{
    /* Retry everything that is waiting immediately. */
    const QList<PurpleAccount *> pending = g_reconnects.keys();
    for (PurpleAccount *account : pending)
        doReconnect(account);
}

void networkDisconnected()
{
    GList *active = purple_accounts_get_all_active();
    for (GList *l = active; l; l = l->next) {
        PurpleAccount *account = static_cast<PurpleAccount *>(l->data);
        if (purple_account_is_disconnected(account))
            continue;
        /* Keep the password across the forced disconnect. */
        gchar *password = g_strdup(purple_account_get_password(account));
        purple_account_disconnect(account);
        purple_account_set_password(account, password);
        g_free(password);
        scheduleReconnect(account);
    }
    g_list_free(active);
}

PurpleConnectionUiOps *connectionUiOps()
{
    static PurpleConnectionUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.connect_progress = connectProgress;
        ops.connected = connected;
        ops.disconnected = disconnected;
        ops.network_connected = networkConnected;
        ops.network_disconnected = networkDisconnected;
        ops.report_disconnect_reason = reportDisconnectReason;
        ready = true;
    }
    return &ops;
}

} // namespace

namespace Core {

bool init(const Options &options, QString *error)
{
    QString dir = options.configDir;
    if (dir.isEmpty())
        dir = QDir(QDir::homePath()).filePath(QStringLiteral(".qtpurple"));
    QDir().mkpath(dir);
    purple_util_set_user_dir(QFile::encodeName(QDir::cleanPath(dir)).constData());

    purple_debug_set_enabled(options.debug ? TRUE : FALSE);

    purple_core_set_ui_ops(coreUiOps());
    purple_eventloop_set_ui_ops(EventLoop::uiOps());

    gchar *plugins = g_build_filename(purple_user_dir(), "plugins", NULL);
    purple_plugins_add_search_path(plugins);
    g_free(plugins);

    if (!purple_core_init(QTPURPLE_UI_ID)) {
        if (error)
            *error = QObject::tr("libpurple initialization failed.");
        return false;
    }

    if (!purple_core_ensure_single_instance()) {
        if (error)
            *error = QObject::tr("Another libpurple client is already running with this configuration directory.");
        purple_core_quit();
        return false;
    }

    purple_set_blist(purple_blist_new());
    purple_blist_load();
    purple_prefs_load();
    purple_prefs_update_old();
    purple_plugins_load_saved(QTPURPLE_PREF_LOADED);
    purple_pounces_load();

    BuddyListWindow::instance()->applyPrefs();
    purple_blist_show();
    TrayIcon::instance()->applyPrefs();

    if (options.noLogin) {
        PurpleSavedStatus *off =
            purple_savedstatus_find_transient_by_type_and_message(PURPLE_STATUS_OFFLINE, nullptr);
        if (!off)
            off = purple_savedstatus_new(nullptr, PURPLE_STATUS_OFFLINE);
        purple_savedstatus_activate(off);
    } else {
        purple_savedstatus_activate(purple_savedstatus_get_startup());
        purple_accounts_restore_current_statuses();
    }

    if (!purple_accounts_get_all())
        AccountsDialog::showDialog();

    return true;
}

void requestQuit()
{
    QCoreApplication::quit();
}

void shutdown()
{
    if (g_shuttingDown)
        return;
    g_shuttingDown = true;

    const QList<PurpleAccount *> pending = g_reconnects.keys();
    for (PurpleAccount *account : pending)
        cancelReconnect(account);

    purple_signals_disconnect_by_handle(&g_signalHandle);

    BuddyListWindow::instance()->saveGeometryPrefs();
    TrayIcon::instance()->shutdown();

    /* Close windows that would otherwise keep pointers into libpurple. */
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets)
        w->hide();

    purple_core_quit();
    Idle::shutdown();
    EventLoop::shutdown();
}

bool shuttingDown()
{
    return g_shuttingDown;
}

void reconnectNow(PurpleAccount *account)
{
    cancelReconnect(account);
    doReconnect(account);
}

} // namespace Core
