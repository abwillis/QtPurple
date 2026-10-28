/*
 * QtPurple - a Qt front end for libpurple
 *
 * Sounds: PurpleSoundUiOps plus the signal hooks that turn IM events into
 * purple_sound_play_event() calls (libpurple itself never plays event
 * sounds; the UI decides when an event happened).
 *
 * Playback methods ("/qtpurple/sound/method"):
 *   automatic - QSoundEffect for WAV files when built with Qt Multimedia,
 *               otherwise an installed player (pw-play, paplay, aplay)
 *   command   - user command; "%s" is replaced by the file name
 *   beep      - QApplication::beep()
 *   none      - silent
 */
#ifndef QTPURPLE_SOUND_H
#define QTPURPLE_SOUND_H

#include "common.h"

#include <QString>

namespace Sound {

struct EventInfo {
    PurpleSoundEventID id;
    const char *pref;           /* key below /qtpurple/sound/{enabled,file}/ */
    const char *label;          /* translatable, see QT_TR_NOOP */
    const char *defaultFile;    /* Pidgin's file name in share/sounds/purple */
    bool defaultEnabled;
};

/* All events, in display order. */
const EventInfo *events(int *count);

/* Register the /qtpurple/sound preferences (called from ui_prefs_init). */
void addPrefs();

PurpleSoundUiOps *uiOps();

QString enabledPref(const EventInfo &e);
QString filePref(const EventInfo &e);

/* Default file for an event, or empty when Pidgin's sounds are not installed. */
QString defaultFile(const EventInfo &e);

/* Custom file if set, otherwise the default file. */
QString effectiveFile(const EventInfo &e);

/* Play a file now, ignoring mute and the per-status rules ("Test" button). */
void test(const QString &file);

bool muted();
void setMuted(bool mute);

/* True when built with Qt Multimedia (volume control is effective). */
bool haveQtMultimedia();

} // namespace Sound

#endif
