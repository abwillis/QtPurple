/*
 * QtPurple - a Qt front end for libpurple
 *
 * XScreenSaver idle query (built only when libXss is available).
 */
#include "idle_x11.h"

#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

static Display *s_display = nullptr;
static XScreenSaverInfo *s_info = nullptr;
static bool s_unavailable = false;

bool idleX11Query(unsigned long *idleMs)
{
    if (s_unavailable)
        return false;

    if (!s_display) {
        /* A private connection: independent of the Qt version/platform API. */
        s_display = XOpenDisplay(nullptr);
        if (!s_display) {
            s_unavailable = true;
            return false;
        }
        int eventBase = 0, errorBase = 0;
        if (!XScreenSaverQueryExtension(s_display, &eventBase, &errorBase) ||
            !(s_info = XScreenSaverAllocInfo())) {
            idleX11Close();
            s_unavailable = true;
            return false;
        }
    }

    if (!XScreenSaverQueryInfo(s_display, DefaultRootWindow(s_display), s_info))
        return false;
    *idleMs = s_info->idle;
    return true;
}

void idleX11Close()
{
    if (s_info) {
        XFree(s_info);
        s_info = nullptr;
    }
    if (s_display) {
        XCloseDisplay(s_display);
        s_display = nullptr;
    }
}
