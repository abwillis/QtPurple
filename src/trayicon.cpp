/*
 * QtPurple - a Qt front end for libpurple
 *
 * System tray icon.
 */
#include "trayicon.h"

#include "accountsdialog.h"
#include "blistwindow.h"
#include "conversationwindow.h"
#include "core.h"
#include "pluginsdialog.h"
#include "pounces.h"
#include "prefsdialog.h"
#include "sound.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStringList>
#include <QTimer>

#include <cstring>

static TrayIcon *s_tray = nullptr;
static int s_signalHandle = 0;

static void scheduleUpdate()
{
    if (s_tray)
        QTimer::singleShot(0, s_tray, []() { if (s_tray) s_tray->update(); });
}

static void onAccountChanged(PurpleAccount *, gpointer)
{
    scheduleUpdate();
}

static void onSavedStatusChanged(PurpleSavedStatus *, PurpleSavedStatus *, gpointer)
{
    scheduleUpdate();
}

static void onTrayPrefChanged(const char *, PurplePrefType, gconstpointer, gpointer)
{
    if (s_tray)
        QTimer::singleShot(0, s_tray, []() { if (s_tray) s_tray->applyPrefs(); });
}

TrayIcon *TrayIcon::instance()
{
    if (!s_tray)
        s_tray = new TrayIcon();
    return s_tray;
}

TrayIcon::TrayIcon()
    : QObject(nullptr)
{
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, &TrayIcon::onBlink);

    void *accounts = purple_accounts_get_handle();
    const char *names[] = { "account-signed-on", "account-signed-off", "account-connecting",
                            "account-enabled", "account-disabled", "account-removed",
                            "account-error-changed" };
    for (const char *name : names)
        purple_signal_connect(accounts, name, &s_signalHandle, PURPLE_CALLBACK(onAccountChanged), nullptr);
    purple_signal_connect(purple_savedstatuses_get_handle(), "savedstatus-changed", &s_signalHandle,
                          PURPLE_CALLBACK(onSavedStatusChanged), nullptr);

    purple_prefs_connect_callback(&s_signalHandle, QTPURPLE_PREF_TRAY_ENABLED, onTrayPrefChanged, nullptr);
    purple_prefs_connect_callback(&s_signalHandle, QTPURPLE_PREF_TRAY_BLINK, onTrayPrefChanged, nullptr);
    purple_prefs_connect_callback(&s_signalHandle, QTPURPLE_PREF_SOUND_MUTE, onTrayPrefChanged, nullptr);

    connect(ConversationWindow::instance(), &ConversationWindow::unreadChanged, this, &TrayIcon::update);
}

void TrayIcon::applyPrefs()
{
    if (m_shutdown)
        return;
    const bool want = purple_prefs_get_bool(QTPURPLE_PREF_TRAY_ENABLED) &&
                      QSystemTrayIcon::isSystemTrayAvailable();

    if (want && !m_tray) {
        m_menu = new QMenu();
        connect(m_menu, &QMenu::aboutToShow, this, &TrayIcon::populateMenu);
        m_tray = new QSystemTrayIcon(this);
        m_tray->setContextMenu(m_menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, &TrayIcon::onActivated);
        populateMenu();
        update();
        m_tray->show();
    } else if (!want && m_tray) {
        m_blinkTimer->stop();
        m_tray->hide();
        m_tray->deleteLater();
        m_tray = nullptr;
        m_menu->deleteLater();
        m_menu = nullptr;
        /* Without a tray icon the buddy list must not stay hidden. */
        BuddyListWindow *w = BuddyListWindow::instance();
        if (!w->isVisible())
            w->show();
    } else {
        update();
    }
}

bool TrayIcon::isShown() const
{
    return m_tray && m_tray->isVisible();
}

void TrayIcon::shutdown()
{
    m_shutdown = true;
    purple_signals_disconnect_by_handle(&s_signalHandle);
    purple_prefs_disconnect_by_handle(&s_signalHandle);
    m_blinkTimer->stop();
    if (m_tray) {
        m_tray->hide();
        delete m_tray;
        m_tray = nullptr;
    }
    delete m_menu;
    m_menu = nullptr;
}

/* ---------------------------------------------------------------- icon */

QIcon TrayIcon::makeIcon(PurpleStatusPrimitive prim, bool connecting, bool message) const
{
    QIcon icon;
    const int sizes[] = { 16, 22, 24, 32, 48, 64 };
    for (int size : sizes) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = size;

        if (message) {
            /* Speech bubble with three dots: unread messages. */
            const QColor c(0x34, 0x65, 0xa4);
            p.setPen(QPen(c.darker(140), qMax(1.0, s / 24.0)));
            p.setBrush(c);
            const QRectF body(s * 0.06, s * 0.12, s * 0.88, s * 0.62);
            p.drawRoundedRect(body, s * 0.16, s * 0.16);
            QPolygonF tail;
            tail << QPointF(s * 0.25, s * 0.70) << QPointF(s * 0.22, s * 0.94) << QPointF(s * 0.48, s * 0.72);
            p.drawPolygon(tail);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            const qreal r = s * 0.065;
            for (int i = 0; i < 3; ++i)
                p.drawEllipse(QPointF(s * (0.30 + 0.20 * i), s * 0.43), r, r);
        } else {
            const QColor c = QtPurple::statusColor(prim);
            const QRectF circle(s * 0.12, s * 0.12, s * 0.76, s * 0.76);
            if (connecting) {
                /* Gray disc with a partial ring: signing on. */
                p.setPen(Qt::NoPen);
                p.setBrush(QtPurple::statusColor(PURPLE_STATUS_OFFLINE));
                p.drawEllipse(circle.adjusted(s * 0.12, s * 0.12, -s * 0.12, -s * 0.12));
                p.setPen(QPen(QColor(0x34, 0x65, 0xa4), qMax(1.5, s / 10.0), Qt::SolidLine, Qt::RoundCap));
                p.setBrush(Qt::NoBrush);
                p.drawArc(circle, 90 * 16, -270 * 16);
            } else if (prim == PURPLE_STATUS_INVISIBLE) {
                p.setPen(QPen(c, qMax(1.5, s / 9.0)));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(circle);
            } else {
                p.setPen(QPen(c.darker(140), qMax(1.0, s / 24.0)));
                p.setBrush(c);
                p.drawEllipse(circle);
            }
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

void TrayIcon::update()
{
    if (!m_tray || m_shutdown)
        return;
    refreshIcon();

    /* Keep the menu current for platforms that do not emit aboutToShow.
     * Deferred: update() can run while one of the menu's own actions is
     * still emitting triggered(), and clear() would delete that action. */
    QTimer::singleShot(0, this, [this]() {
        if (m_menu && !m_menu->isVisible())
            populateMenu();
    });
}

void TrayIcon::refreshIcon()
{
    if (!m_tray)
        return;

    bool connecting = false;
    bool connected = false;
    QStringList accountLines;
    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        PurpleAccount *account = static_cast<PurpleAccount *>(l->data);
        if (!purple_account_get_enabled(account, QTPURPLE_UI_ID))
            continue;
        if (purple_account_is_connecting(account))
            connecting = true;
        if (purple_account_is_connected(account))
            connected = true;
        PurpleStatus *status = purple_account_get_active_status(account);
        QString state;
        if (purple_account_is_connected(account) && status)
            state = qs(purple_status_get_name(status));
        else if (purple_account_is_connecting(account))
            state = tr("Connecting");
        else
            state = tr("Offline");
        accountLines << QStringLiteral("%1: %2").arg(QtPurple::accountDisplayName(account), state);
    }

    PurpleStatusPrimitive prim = PURPLE_STATUS_OFFLINE;
    PurpleSavedStatus *saved = purple_savedstatus_get_current();
    if (connected && saved)
        prim = purple_savedstatus_get_type(saved);

    const int unread = ConversationWindow::instance()->unreadCount();
    const bool blink = purple_prefs_get_bool(QTPURPLE_PREF_TRAY_BLINK);
    if (unread > 0 && blink) {
        if (!m_blinkTimer->isActive())
            m_blinkTimer->start();
    } else {
        m_blinkTimer->stop();
        m_blinkPhase = false;
    }
    const bool showMessage = unread > 0 && (!blink || !m_blinkPhase);
    m_tray->setIcon(makeIcon(prim, connecting && !connected, showMessage));

    QString tip = QStringLiteral(QTPURPLE_NAME);
    if (unread > 0)
        tip += QStringLiteral("\n") + tr("%n conversation(s) with unread messages", nullptr, unread);
    if (!accountLines.isEmpty())
        tip += QStringLiteral("\n") + accountLines.join(QStringLiteral("\n"));
    if (Sound::muted())
        tip += QStringLiteral("\n") + tr("Sounds muted");
    m_tray->setToolTip(tip);
}

void TrayIcon::onBlink()
{
    m_blinkPhase = !m_blinkPhase;
    refreshIcon();      /* icon only: no menu rebuild every half second */
}

/* --------------------------------------------------------- interaction */

void TrayIcon::toggleBuddyList()
{
    BuddyListWindow *w = BuddyListWindow::instance();
    if (w->isVisible() && !w->isMinimized()) {
        w->saveGeometryPrefs();
        w->hide();
    } else {
        w->showNormal();
        w->raise();
        w->activateWindow();
    }
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason != QSystemTrayIcon::Trigger)
        return;
    if (!ConversationWindow::instance()->presentFirstUnread())
        toggleBuddyList();
}

void TrayIcon::populateMenu()
{
    if (!m_menu)
        return;
    m_menu->clear();

    BuddyListWindow *blist = BuddyListWindow::instance();
    QAction *show = m_menu->addAction(tr("Show &Buddy List"));
    show->setCheckable(true);
    show->setChecked(blist->isVisible() && !blist->isMinimized());
    connect(show, &QAction::triggered, this, [this]() { toggleBuddyList(); });

    const int unread = ConversationWindow::instance()->unreadCount();
    if (unread > 0) {
        m_menu->addAction(tr("Show &Unread Messages (%1)").arg(unread), this, []() {
            ConversationWindow::instance()->presentFirstUnread();
        });
    }
    m_menu->addSeparator();

    m_menu->addAction(tr("New &Message..."), this, []() {
        BuddyListWindow::instance()->showNewImDialog();
    });
    m_menu->addAction(tr("Join a &Chat..."), this, []() {
        BuddyListWindow::instance()->showJoinChatDialog();
    });

    QMenu *status = m_menu->addMenu(tr("Change &Status"));
    QActionGroup *group = new QActionGroup(status);
    PurpleSavedStatus *saved = purple_savedstatus_get_current();
    const PurpleStatusPrimitive current = saved ? purple_savedstatus_get_type(saved) : PURPLE_STATUS_OFFLINE;
    const struct { PurpleStatusPrimitive prim; const char *label; } entries[] = {
        { PURPLE_STATUS_AVAILABLE,     QT_TR_NOOP("Available") },
        { PURPLE_STATUS_AWAY,          QT_TR_NOOP("Away") },
        { PURPLE_STATUS_UNAVAILABLE,   QT_TR_NOOP("Do not disturb") },
        { PURPLE_STATUS_EXTENDED_AWAY, QT_TR_NOOP("Extended away") },
        { PURPLE_STATUS_INVISIBLE,     QT_TR_NOOP("Invisible") },
        { PURPLE_STATUS_OFFLINE,       QT_TR_NOOP("Offline") },
    };
    for (const auto &e : entries) {
        QAction *a = status->addAction(QtPurple::statusIcon(e.prim), tr(e.label));
        a->setCheckable(true);
        a->setChecked(e.prim == current);
        group->addAction(a);
        const PurpleStatusPrimitive prim = e.prim;
        connect(a, &QAction::triggered, this, [prim]() {
            BuddyListWindow::instance()->setStatusPrimitive(prim);
        });
    }
    m_menu->addSeparator();

    m_menu->addAction(tr("&Accounts"), this, []() { AccountsDialog::showDialog(); });
    m_menu->addAction(tr("Buddy &Pounces"), this, []() { Pounces::showManager(); });
    m_menu->addAction(tr("Plu&gins"), this, []() { PluginsDialog::showDialog(); });
    m_menu->addAction(tr("Pr&eferences"), this, []() { PreferencesDialog::showDialog(); });

    QAction *mute = m_menu->addAction(tr("Mute &Sounds"));
    mute->setCheckable(true);
    mute->setChecked(Sound::muted());
    connect(mute, &QAction::toggled, this, [](bool on) { Sound::setMuted(on); });

    m_menu->addSeparator();
    m_menu->addAction(tr("&Quit"), this, []() { Core::requestQuit(); });
}
