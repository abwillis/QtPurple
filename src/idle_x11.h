/*
 * QtPurple - a Qt front end for libpurple
 *
 * XScreenSaver idle query. Kept in its own translation unit so the Xlib
 * macros (None, Bool, Status, ...) never meet the Qt headers.
 */
#ifndef QTPURPLE_IDLE_X11_H
#define QTPURPLE_IDLE_X11_H

/* Milliseconds since the last keyboard/mouse input on the X display.
 * Returns false when no X display or no MIT-SCREEN-SAVER extension. */
bool idleX11Query(unsigned long *idleMs);

/* Close the private X connection. */
void idleX11Close();

#endif
