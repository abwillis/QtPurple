/*
 * QtPurple - a Qt front end for libpurple
 *
 * System idle detection (PurpleIdleUiOps).
 */
#include "idle.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>

#ifdef QTPURPLE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#endif

#ifdef QTPURPLE_HAVE_XSS
#include "idle_x11.h"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstring>

namespace {

enum Backend { Unresolved, WindowsApi, MutterDBus, X11Xss, AppActivity };

Backend g_backend = Unresolved;
QElapsedTimer g_clock;
qint64 g_lastActivityMs = 0;

/* ------------------------------------------------------- app activity */

class ActivityFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
            g_lastActivityMs = g_clock.elapsed();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
};

ActivityFilter *g_filter = nullptr;

qint64 appIdleMs()
{
    if (!g_clock.isValid())
        return 0;
    return g_clock.elapsed() - g_lastActivityMs;
}

/* ------------------------------------------------------------ backends */

#ifdef QTPURPLE_HAVE_DBUS
const char kMutterService[] = "org.gnome.Mutter.IdleMonitor";

bool mutterQuery(qint64 *ms)
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(kMutterService),
        QStringLiteral("/org/gnome/Mutter/IdleMonitor/Core"),
        QStringLiteral("org.gnome.Mutter.IdleMonitor"),
        QStringLiteral("GetIdletime"));
    /* Short timeout: this runs on the GUI thread every few seconds while idle. */
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 250);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    *ms = qint64(reply.arguments().first().toULongLong());
    return true;
}

bool mutterAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    const QDBusReply<bool> reg = bus.interface()->isServiceRegistered(QString::fromLatin1(kMutterService));
    if (!reg.isValid() || !reg.value())
        return false;
    qint64 ms = 0;
    return mutterQuery(&ms);
}
#endif

#ifdef QTPURPLE_HAVE_XSS
bool x11Query(qint64 *ms)
{
    unsigned long idle = 0;
    if (!idleX11Query(&idle))
        return false;
    *ms = qint64(idle);
    return true;
}

bool x11Available()
{
    /* Under XWayland the X server only sees input sent to X clients, so
     * only trust XScreenSaver on a real X11 session. */
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return false;
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return false;
    qint64 ms = 0;
    return x11Query(&ms);
}
#endif

void resolveBackend()
{
#ifdef _WIN32
    g_backend = WindowsApi;
    return;
#else
#ifdef QTPURPLE_HAVE_DBUS
    if (mutterAvailable()) {
        g_backend = MutterDBus;
        return;
    }
#endif
#ifdef QTPURPLE_HAVE_XSS
    if (x11Available()) {
        g_backend = X11Xss;
        return;
    }
#endif
    g_backend = AppActivity;
#endif
}

bool queryBackend(qint64 *ms)
{
    switch (g_backend) {
#ifdef _WIN32
    case WindowsApi: {
        LASTINPUTINFO lii;
        memset(&lii, 0, sizeof(lii));
        lii.cbSize = sizeof(lii);
        if (!GetLastInputInfo(&lii))
            return false;
        *ms = qint64(DWORD(GetTickCount() - lii.dwTime));   /* wraps correctly */
        return true;
    }
#endif
#ifdef QTPURPLE_HAVE_DBUS
    case MutterDBus:
        return mutterQuery(ms);
#endif
#ifdef QTPURPLE_HAVE_XSS
    case X11Xss:
        return x11Query(ms);
#endif
    case AppActivity:
        *ms = appIdleMs();
        return true;
    default:
        return false;
    }
}

time_t getTimeIdle()
{
    if (g_backend == Unresolved)
        resolveBackend();

    qint64 ms = 0;
    if (!queryBackend(&ms)) {
        /* The session service went away (e.g. compositor restart):
         * use the in-application fallback from now on. */
        purple_debug_warning("idle", "System idle query failed; using application activity\n");
        g_backend = AppActivity;
        ms = appIdleMs();
    }
    if (ms < 0)
        ms = 0;
    return time_t(ms / 1000);
}

} // namespace

namespace Idle {

PurpleIdleUiOps *uiOps()
{
    static PurpleIdleUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.get_time_idle = getTimeIdle;
        ready = true;
    }
    return &ops;
}

void init()
{
    if (g_filter)
        return;
    g_clock.start();
    g_lastActivityMs = 0;
    g_filter = new ActivityFilter(QCoreApplication::instance());
    QCoreApplication::instance()->installEventFilter(g_filter);
}

void shutdown()
{
    if (g_filter) {
        QCoreApplication::instance()->removeEventFilter(g_filter);
        delete g_filter;
        g_filter = nullptr;
    }
#ifdef QTPURPLE_HAVE_XSS
    idleX11Close();
#endif
    g_backend = Unresolved;
}

QString backendName()
{
    if (g_backend == Unresolved)
        resolveBackend();
    switch (g_backend) {
    case WindowsApi:  return QCoreApplication::translate("Idle", "Windows input activity");
    case MutterDBus:  return QCoreApplication::translate("Idle", "GNOME idle monitor (D-Bus)");
    case X11Xss:      return QCoreApplication::translate("Idle", "X11 screen saver extension");
    case AppActivity: return QCoreApplication::translate("Idle", "Activity in QtPurple windows only");
    default:          return QString();
    }
}

} // namespace Idle
