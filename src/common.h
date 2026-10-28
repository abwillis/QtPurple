/*
 * QtPurple - a Qt front end for libpurple
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef QTPURPLE_COMMON_H
#define QTPURPLE_COMMON_H

#include <purple.h>

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QString>
#include <QUrl>

#define QTPURPLE_UI_ID          "qtpurple"
#define QTPURPLE_NAME           "QtPurple"
#define QTPURPLE_VERSION        "0.5.1"
#define QTPURPLE_WEBSITE        "https://github.com/"

#define QTPURPLE_PREFS_ROOT     "/qtpurple"
#define QTPURPLE_PREF_PLUGINS   "/qtpurple/plugins"
#define QTPURPLE_PREF_LOADED    "/qtpurple/plugins/loaded"
#define QTPURPLE_PREF_BLIST     "/qtpurple/blist"
#define QTPURPLE_PREF_OFFLINE   "/qtpurple/blist/show_offline_buddies"
#define QTPURPLE_PREF_EMPTY     "/qtpurple/blist/show_empty_groups"
#define QTPURPLE_PREF_WIDTH     "/qtpurple/blist/width"
#define QTPURPLE_PREF_HEIGHT    "/qtpurple/blist/height"
#define QTPURPLE_PREF_CONV      "/qtpurple/conversations"
#define QTPURPLE_PREF_CONV_W    "/qtpurple/conversations/width"
#define QTPURPLE_PREF_CONV_H    "/qtpurple/conversations/height"
#define QTPURPLE_PREF_SHOW_REACTIONS "/qtpurple/conversations/show_reactions"
#define QTPURPLE_PREF_EMOJI_CODES    "/qtpurple/conversations/emoji_codes"
#define QTPURPLE_PREF_SHOW_TOOLBAR   "/qtpurple/conversations/show_toolbar"
#define QTPURPLE_PREF_TIMESTAMPS     "/qtpurple/conversations/show_timestamps"
#define QTPURPLE_PREF_FONT_FACE      "/qtpurple/conversations/font_face"
#define QTPURPLE_PREF_FONT_COLOR     "/qtpurple/conversations/font_color"
#define QTPURPLE_PREF_FONT_SIZE      "/qtpurple/conversations/font_size"

#define QTPURPLE_PREF_SOUND         "/qtpurple/sound"
#define QTPURPLE_PREF_SOUND_ENABLED "/qtpurple/sound/enabled"
#define QTPURPLE_PREF_SOUND_FILE    "/qtpurple/sound/file"
#define QTPURPLE_PREF_SOUND_METHOD  "/qtpurple/sound/method"
#define QTPURPLE_PREF_SOUND_COMMAND "/qtpurple/sound/command"
#define QTPURPLE_PREF_SOUND_VOLUME  "/qtpurple/sound/volume"
#define QTPURPLE_PREF_SOUND_MUTE    "/qtpurple/sound/mute"
#define QTPURPLE_PREF_SOUND_FOCUS   "/qtpurple/sound/conv_focus"
#define QTPURPLE_PREF_TRAY          "/qtpurple/tray"
#define QTPURPLE_PREF_TRAY_ENABLED  "/qtpurple/tray/enabled"
#define QTPURPLE_PREF_TRAY_BLINK    "/qtpurple/tray/blink"

/* Convert a (possibly NULL) UTF-8 C string coming from libpurple. */
inline QString qs(const char *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

/*
 * Holds a UTF-8 copy of a QString for the lifetime of the object so the
 * pointer can safely be handed to libpurple (which copies what it keeps).
 */
class Utf8
{
public:
    explicit Utf8(const QString &s) : m_data(s.toUtf8()) {}
    const char *get() const { return m_data.constData(); }
    /* NULL when the string is empty - handy for optional libpurple args. */
    const char *orNull() const { return m_data.isEmpty() ? nullptr : m_data.constData(); }
    operator const char *() const { return m_data.constData(); }
private:
    QByteArray m_data;
};

namespace QtPurple {

QColor statusColor(PurpleStatusPrimitive prim);
QIcon statusIcon(PurpleStatusPrimitive prim, bool idle = false);
QIcon chatIcon();
QIcon protocolIcon(PurpleAccount *account);
QString primitiveName(PurpleStatusPrimitive prim);

/* TRUE when the pointer still refers to a registered account. */
bool accountExists(PurpleAccount *account);

/* TRUE when the pointer still refers to a live connection. */
bool connectionExists(PurpleConnection *gc);

/* "user (Protocol)" */
QString accountDisplayName(PurpleAccount *account);

/* Convert GTK-style "_Mnemonic" labels to Qt-style "&Mnemonic". */
QString mnemonicToQt(const char *text);

/* Strip GTK-style mnemonics entirely ("_Room:" -> "Room:"). */
QString stripMnemonic(const char *text);

/* Open a link from untrusted content: web/mail/xmpp/irc links are opened,
 * file: links only reveal the containing folder, everything else is ignored.
 * Returns true when something was opened. */
bool openLink(const QUrl &url);

/* Status primitive of the account's active status (OFFLINE when disconnected). */
PurpleStatusPrimitive accountPrimitive(PurpleAccount *account);

} // namespace QtPurple

#endif
