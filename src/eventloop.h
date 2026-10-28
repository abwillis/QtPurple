/*
 * QtPurple - a Qt front end for libpurple
 *
 * libpurple event loop integration on top of the Qt event loop
 * (QTimer + QSocketNotifier), so it does not depend on Qt being built
 * with the GLib event dispatcher.
 */
#ifndef QTPURPLE_EVENTLOOP_H
#define QTPURPLE_EVENTLOOP_H

#include "common.h"

namespace EventLoop {

/* Must be called from the GUI thread after QApplication is constructed. */
PurpleEventLoopUiOps *uiOps();

/* Removes every pending timer/input. Called after purple_core_quit(). */
void shutdown();

} // namespace EventLoop

#endif
