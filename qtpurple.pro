# qmake project for QtPurple (Qt 5.10+ or Qt 6).
#
#   qmake && make
#
# Without pkg-config (e.g. some OS/2 or Windows setups) pass the install
# prefixes explicitly:
#
#   qmake PURPLE_PREFIX=/usr/local GLIB_PREFIX=/usr/local
#
TEMPLATE = app
TARGET   = qtpurple
QT      += widgets
CONFIG  += c++17 no_keywords warn_on
DEFINES += QT_NO_KEYWORDS

# Optional pieces; disable with  CONFIG+=no_multimedia / no_dbus / no_xss
!no_multimedia:qtHaveModule(multimedia) {
    QT      += multimedia
    DEFINES += QTPURPLE_HAVE_QTMULTIMEDIA
}
unix:!macx:!no_dbus:qtHaveModule(dbus) {
    QT      += dbus
    DEFINES += QTPURPLE_HAVE_DBUS
}

isEmpty(PURPLE_PREFIX) {
    CONFIG    += link_pkgconfig
    PKGCONFIG += purple glib-2.0 gobject-2.0
    unix:!macx:!no_xss:packagesExist(x11 xscrnsaver) {
        PKGCONFIG += x11 xscrnsaver
        DEFINES   += QTPURPLE_HAVE_XSS
        HEADERS   += src/idle_x11.h
        SOURCES   += src/idle_x11.cpp
    }
} else {
    isEmpty(GLIB_PREFIX): GLIB_PREFIX = $$PURPLE_PREFIX
    INCLUDEPATH += $$PURPLE_PREFIX/include/libpurple \
                   $$GLIB_PREFIX/include/glib-2.0 \
                   $$GLIB_PREFIX/lib/glib-2.0/include
    LIBS += -L$$PURPLE_PREFIX/lib -L$$GLIB_PREFIX/lib -lpurple -lgobject-2.0 -lglib-2.0
}

HEADERS += \
    src/common.h \
    src/core.h \
    src/eventloop.h \
    src/blistwindow.h \
    src/blistdialogs.h \
    src/accountsdialog.h \
    src/conversationwindow.h \
    src/requestui.h \
    src/notifyui.h \
    src/transferswindow.h \
    src/pluginsdialog.h \
    src/debugwindow.h \
    src/sound.h \
    src/idle.h \
    src/pounces.h \
    src/trayicon.h \
    src/prefsdialog.h \
    src/emoji.h

SOURCES += \
    src/main.cpp \
    src/common.cpp \
    src/core.cpp \
    src/eventloop.cpp \
    src/blistwindow.cpp \
    src/blistdialogs.cpp \
    src/accountsdialog.cpp \
    src/conversationwindow.cpp \
    src/requestui.cpp \
    src/notifyui.cpp \
    src/transferswindow.cpp \
    src/pluginsdialog.cpp \
    src/debugwindow.cpp \
    src/sound.cpp \
    src/idle.cpp \
    src/pounces.cpp \
    src/trayicon.cpp \
    src/prefsdialog.cpp \
    src/emoji.cpp
