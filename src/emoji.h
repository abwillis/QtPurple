/*
 * QtPurple - a Qt front end for libpurple
 *
 * Emoji and reaction handling for the conversation view.
 *
 *  - Emoji images that a protocol sends as <img src="https://..."> are
 *    never loaded (remote content is blocked), so they are replaced by
 *    their alt/title text, which is usually the emoji itself or a
 *    ":shortcode:".
 *  - ":shortcode:" text is turned into the Unicode emoji when the code is
 *    known: a built-in table plus the user's own <config dir>/emoji.txt.
 *  - Reaction notices ("Reacted with a :rainbowsmile:") are recognised so
 *    they can be hidden per conversation.
 */
#ifndef QTPURPLE_EMOJI_H
#define QTPURPLE_EMOJI_H

#include "common.h"

#include <QString>

namespace Emoji {

/* Register the default preferences (called from ui_prefs_init). */
void addPrefs();

/* TRUE when the message is a reaction notice rather than normal text. */
bool isReaction(const QString &html);

/* Replace blocked <img> tags by their alt/title text and, when
 * convertCodes is set, ":shortcode:" text by Unicode emoji. */
QString render(const QString &html, bool convertCodes);

/* <config dir>/emoji.txt; one "name = emoji" per line, '#' comments. */
QString userMapPath();

} // namespace Emoji

#endif
