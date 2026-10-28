/*
 * QtPurple - a Qt front end for libpurple
 *
 * libpurple event loop integration on top of the Qt event loop.
 *
 * Notes:
 *  - Timeouts may be added/removed from a non-GUI thread (libpurple's
 *    Win32 DNS resolver does this), so the bookkeeping is mutex protected
 *    and Qt objects are always created/destroyed on the GUI thread.
 *  - Callbacks are never invoked while the lock is held, because a
 *    callback is allowed to add or remove sources (including itself).
 *  - QSocketNotifier::activated() has different overloads in Qt 5.15 and
 *    Qt 6, so the notifier subclass handles QEvent::SockAct directly.
 */
#include "eventloop.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <climits>
#include <cstring>

namespace {

struct TimeoutSource {
    GSourceFunc func = nullptr;
    gpointer data = nullptr;
    guint interval = 0;
    QTimer *timer = nullptr;
};

class InputNotifier;

struct InputSource {
    PurpleInputFunction func = nullptr;
    gpointer data = nullptr;
    int fd = -1;
    PurpleInputCondition cond = PurpleInputCondition(0);
    InputNotifier *read = nullptr;
    InputNotifier *write = nullptr;
};

QMutex g_lock;
QHash<guint, TimeoutSource *> g_timeouts;
QHash<guint, InputSource *> g_inputs;
guint g_nextId = 1;

bool isGuiThread()
{
    QCoreApplication *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

/* Caller must hold g_lock. */
guint allocId()
{
    guint id;
    do {
        id = g_nextId++;
        if (g_nextId == 0)
            g_nextId = 1;
    } while (id == 0 || g_timeouts.contains(id) || g_inputs.contains(id));
    return id;
}

/* ---------------------------------------------------------------- timers */

void fireTimeout(guint id);

void startTimeout(guint id)
{
    QMutexLocker locker(&g_lock);
    TimeoutSource *src = g_timeouts.value(id, nullptr);
    if (!src || src->timer)
        return;

    QTimer *timer = new QTimer();
    const guint ms = src->interval > guint(INT_MAX) ? guint(INT_MAX) : src->interval;
    timer->setInterval(int(ms));
    timer->setTimerType(ms >= 1000 ? Qt::CoarseTimer : Qt::PreciseTimer);
    QObject::connect(timer, &QTimer::timeout, timer, [id]() { fireTimeout(id); });
    src->timer = timer;
    timer->start();
}

void destroyTimer(QTimer *timer)
{
    if (!timer)
        return;
    if (isGuiThread()) {
        timer->stop();
        timer->deleteLater();
    } else {
        QMetaObject::invokeMethod(timer, [timer]() {
            timer->stop();
            timer->deleteLater();
        }, Qt::QueuedConnection);
    }
}

gboolean timeoutRemove(guint id)
{
    TimeoutSource *src;
    {
        QMutexLocker locker(&g_lock);
        src = g_timeouts.take(id);
    }
    if (!src)
        return FALSE;
    destroyTimer(src->timer);
    delete src;
    return TRUE;
}

void fireTimeout(guint id)
{
    GSourceFunc func;
    gpointer data;
    {
        QMutexLocker locker(&g_lock);
        TimeoutSource *src = g_timeouts.value(id, nullptr);
        if (!src)
            return;
        func = src->func;
        data = src->data;
    }

    /* The callback may remove this timeout or add new ones. */
    if (!func(data))
        timeoutRemove(id);
}

guint timeoutAddMs(guint interval, GSourceFunc function, gpointer data)
{
    TimeoutSource *src = new TimeoutSource;
    src->func = function;
    src->data = data;
    src->interval = interval;

    guint id;
    {
        QMutexLocker locker(&g_lock);
        id = allocId();
        g_timeouts.insert(id, src);
    }

    if (isGuiThread()) {
        startTimeout(id);
    } else if (QCoreApplication *app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, [id]() { startTimeout(id); }, Qt::QueuedConnection);
    }
    return id;
}

guint timeoutAdd(guint interval, GSourceFunc function, gpointer data)
{
    return timeoutAddMs(interval, function, data);
}

guint timeoutAddSeconds(guint interval, GSourceFunc function, gpointer data)
{
    const guint maxSeconds = guint(INT_MAX) / 1000u;
    if (interval > maxSeconds)
        interval = maxSeconds;
    return timeoutAddMs(interval * 1000u, function, data);
}

/* ---------------------------------------------------------------- inputs */

void fireInput(guint id, PurpleInputCondition cond)
{
    PurpleInputFunction func;
    gpointer data;
    int fd;
    {
        QMutexLocker locker(&g_lock);
        InputSource *src = g_inputs.value(id, nullptr);
        if (!src)
            return;
        func = src->func;
        data = src->data;
        fd = src->fd;
    }
    /* The callback may call purple_input_remove() on this very source. */
    func(data, fd, cond);
}

class InputNotifier : public QSocketNotifier
{
public:
    InputNotifier(qintptr fd, QSocketNotifier::Type type, guint id, PurpleInputCondition cond)
        : QSocketNotifier(fd, type), m_id(id), m_cond(cond) {}

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::SockAct || e->type() == QEvent::SockClose) {
            fireInput(m_id, m_cond);
            return true;
        }
        return QSocketNotifier::event(e);
    }

private:
    guint m_id;
    PurpleInputCondition m_cond;
};

void startInput(guint id)
{
    QMutexLocker locker(&g_lock);
    InputSource *src = g_inputs.value(id, nullptr);
    if (!src || src->read || src->write)
        return;

    if (src->cond & PURPLE_INPUT_READ)
        src->read = new InputNotifier(src->fd, QSocketNotifier::Read, id, PURPLE_INPUT_READ);
    if (src->cond & PURPLE_INPUT_WRITE)
        src->write = new InputNotifier(src->fd, QSocketNotifier::Write, id, PURPLE_INPUT_WRITE);
}

void destroyNotifier(InputNotifier *n)
{
    if (!n)
        return;
    if (isGuiThread()) {
        n->setEnabled(false);
        n->deleteLater();      /* never delete inside its own event() */
    } else {
        QMetaObject::invokeMethod(n, [n]() {
            n->setEnabled(false);
            n->deleteLater();
        }, Qt::QueuedConnection);
    }
}

guint inputAdd(int fd, PurpleInputCondition cond, PurpleInputFunction func, gpointer data)
{
    if (fd < 0 || !func || !(cond & (PURPLE_INPUT_READ | PURPLE_INPUT_WRITE)))
        return 0;

    InputSource *src = new InputSource;
    src->func = func;
    src->data = data;
    src->fd = fd;
    src->cond = cond;

    guint id;
    {
        QMutexLocker locker(&g_lock);
        id = allocId();
        g_inputs.insert(id, src);
    }

    if (isGuiThread()) {
        startInput(id);
    } else if (QCoreApplication *app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, [id]() { startInput(id); }, Qt::QueuedConnection);
    }
    return id;
}

gboolean inputRemove(guint id)
{
    InputSource *src;
    {
        QMutexLocker locker(&g_lock);
        src = g_inputs.take(id);
    }
    if (!src)
        return FALSE;
    destroyNotifier(src->read);
    destroyNotifier(src->write);
    delete src;
    return TRUE;
}

PurpleEventLoopUiOps g_ops;
bool g_opsReady = false;

} // namespace

namespace EventLoop {

PurpleEventLoopUiOps *uiOps()
{
    if (!g_opsReady) {
        memset(&g_ops, 0, sizeof(g_ops));
        g_ops.timeout_add = timeoutAdd;
        g_ops.timeout_remove = timeoutRemove;
        g_ops.input_add = inputAdd;
        g_ops.input_remove = inputRemove;
        g_ops.input_get_error = nullptr;      /* libpurple falls back to getsockopt() */
        g_ops.timeout_add_seconds = timeoutAddSeconds;
        g_opsReady = true;
    }
    return &g_ops;
}

void shutdown()
{
    QList<guint> timeouts;
    QList<guint> inputs;
    {
        QMutexLocker locker(&g_lock);
        timeouts = g_timeouts.keys();
        inputs = g_inputs.keys();
    }
    for (guint id : timeouts)
        timeoutRemove(id);
    for (guint id : inputs)
        inputRemove(id);
}

} // namespace EventLoop
