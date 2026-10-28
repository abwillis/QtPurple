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

isEmpty(PURPLE_PREFIX) {
    CONFIG    += link_pkgconfig
    PKGCONFIG += purple glib-2.0 gobject-2.0
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
    src/debugwindow.h

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
    src/debugwindow.cpp
