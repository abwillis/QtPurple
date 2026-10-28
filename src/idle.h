/*
 * QtPurple - a Qt front end for libpurple
 *
 * System idle detection (PurpleIdleUiOps). libpurple polls get_time_idle()
 * when "/purple/away/idle_reporting" is "system" and uses it for idle
 * reporting and auto-away.
 *
 * Backends, first available wins:
 *   Windows       GetLastInputInfo()
 *   GNOME (Mutter) org.gnome.Mutter.IdleMonitor over D-Bus (X11 and Wayland)
 *   X11           MIT-SCREEN-SAVER extension (libXss)
 *   fallback      keyboard/mouse activity inside QtPurple's own windows
 */
#ifndef QTPURPLE_IDLE_H
#define QTPURPLE_IDLE_H

#include "common.h"

#include <QString>

namespace Idle {

PurpleIdleUiOps *uiOps();

/* Start tracking activity in QtPurple's windows (fallback backend). */
void init();

/* Release backend resources. */
void shutdown();

/* Human readable name of the backend in use. */
QString backendName();

} // namespace Idle

#endif
