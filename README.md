# QtPurple

A Qt instant-messaging client built on **libpurple** — the protocol library used by Pidgin — in the same way Pidgin is a GTK front end to it. QtPurple supports every protocol plugin that the installed libpurple provides (XMPP, IRC, Bonjour, Gadu-Gadu, SIMPLE, Zephyr, Novell, plus any third-party prpl).

Targets libpurple **2.x** (the API was checked against the 2.14.14 headers) and **Qt 5.10+ or Qt 6**.

## Features

**Buddy list**
- Groups, contacts (multi-buddy contacts expand), chats, status icons, idle time, protocol status text
- Protocol-supplied tooltips
- Show offline buddies / empty groups, per-buddy "show when offline"
- Context menus: Send IM, Send File, Get Info, Alias, Move to group, Remove, plus protocol-specific and plugin-extended actions
- Add Buddy, Add Chat, Add Group, Join Chat, New IM, Get User Info
- Status selector (Available, Away, Do not disturb, Extended away, Invisible, Offline) with status message (libpurple saved statuses)

**Accounts**
- Account manager: add, modify, delete, enable/disable, live status and error display
- Editor driven by the protocol: user splits (e.g. `user@server/resource`), all protocol options (boolean, integer, string, list), remember password, mail notification, create account on server
- Accounts menu with each connected protocol's actions (e.g. XMPP "Set Mood…", "Change Password…")
- Automatic reconnect with exponential back-off for non-fatal errors; a Reconnect / Modify Account dialog for fatal ones; network up/down handling
- Authorization requests ("X wants to add you") with "add to my list"

**Conversations**
- Tabbed window for IMs and chats, timestamps, `/me`, auto-linked URLs, inline images from libpurple's image store
- Typing notifications in both directions
- Chat user list ranked by founder/op/half-op/voice, IM/Info/Ignore/Mention per user, Tab nick completion, topic display
- Slash commands through libpurple's command API (`/help`, `/join`, `/topic`, …); `//text` sends a literal slash
- Buddy icon and status in the header, unread/typing tab colours, window alert for new IMs and nick mentions
- Send File, Get Info, Add Buddy, Invite, Clear Scrollback
- Reactions and emoji: emoji images sent as web links show their text (the emoji itself or a `:code:`), `:code:` text becomes the Unicode emoji (built-in table plus `<config dir>/emoji.txt`), and reaction notices ("Reacted with …") are shown in italics; both can be switched per conversation in the **Conversation** menu, with defaults under Preferences → Conversations

**libpurple request/notify UI**
- Input, choice, action, multi-field forms (string, integer, boolean, choice, list, label, image, account), file and folder dialogs
- Messages, e-mail notifications, formatted text, user info, search results with protocol buttons, URIs

**Buddy pounces**
- Pounce manager (Tools → Buddy Pounces) and editor; "Add Buddy Pounce…" in the buddy context menu
- Events: signs on/off, goes away/returns, becomes idle/returns, starts/pauses/stops typing, sends a message
- Actions: open an IM window, pop up a notification, send a message, execute a command, play a sound
- Options: only when not available, recurring (otherwise the pounce is removed after it fires)

**Sounds**
- Buddy logs in/out, message received (and first message of a conversation), message sent, chat join/leave/talk, nick said, attention, default pounce sound
- Per-event enable and custom file; Pidgin's sound files are used when installed (`share/sounds/purple`)
- Methods: automatic (Qt Multimedia for WAV, else `pw-play`/`paplay`/`aplay`), custom command, console beep, none
- Volume (Qt Multimedia), mute (Ctrl+S), "sounds while" available/away/always, optional silence for the focused conversation, no buddy log-in/log-out flood for 10 s after signing on

**System tray**
- Icon shows the global status, a connecting state, and unread messages (optionally blinking)
- Click: open the first unread conversation, otherwise show/hide the buddy list
- Menu: show buddy list, unread messages, new message, join chat, change status, accounts, pounces, plugins, preferences, mute sounds, quit
- While the icon is shown, closing the buddy list hides it

**Idle detection**
- System idle time for libpurple's idle reporting and auto-away
- Backends: Windows `GetLastInputInfo`, GNOME idle monitor over D-Bus (X11 and Wayland), X11 screen saver extension (libXss), fallback to keyboard/mouse activity in QtPurple's own windows
- Preferences: report idle time (never / from last sent message / keyboard and mouse), minutes before idle, change to Away when idle, auto-reply

**Other**
- Preferences dialog (Ctrl+P): conversations, sounds, away/idle, system tray
- File transfer window (progress, cancel, open containing folder)
- Plugin manager with a generic preference editor for plugins that provide a preference frame
- Debug window fed by libpurple's debug output

## Building

### openSUSE Tumbleweed

```bash
sudo zypper install libpurple-devel glib2-devel cmake gcc-c++ qt6-base-devel
# or, for Qt 5:  libqt5-qtbase-devel

# optional
sudo zypper install qt6-multimedia-devel libXss-devel    # Qt 5: libqt5-qtmultimedia-devel
```

Optional parts are used when found:

| Part | Gives | CMake switch | qmake switch |
|---|---|---|---|
| Qt Multimedia | in-process WAV playback with volume | `-DQTPURPLE_WITH_MULTIMEDIA=OFF` | `CONFIG+=no_multimedia` |
| Qt D-Bus | GNOME idle monitor (works on Wayland) | `-DQTPURPLE_WITH_DBUS=OFF` | `CONFIG+=no_dbus` |
| libXss + libX11 | X11 idle time | `-DQTPURPLE_WITH_XSS=OFF` | `CONFIG+=no_xss` |

Qt D-Bus is part of `qt6-base-devel` on Tumbleweed. CMake prints which parts were enabled.

CMake (prefers Qt 6; add `-DQTPURPLE_QT5=ON` to force Qt 5):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
./build/qtpurple
```

qmake:

```bash
qmake qtpurple.pro      # or qmake6
make -j"$(nproc)"
```

Without pkg-config, give the prefixes explicitly:

```bash
qmake PURPLE_PREFIX=/usr/local GLIB_PREFIX=/usr/local
```

## Running

```
qtpurple [-c DIR] [-d] [-n]
  -c, --config DIR   configuration directory (default: ~/.qtpurple)
  -d, --debug        print libpurple debug output to stdout
  -n, --nologin      start with the Offline status
```

QtPurple uses its own configuration directory so it never rewrites Pidgin's files while Pidgin is running. To reuse Pidgin's accounts and buddy list, copy `accounts.xml`, `blist.xml`, and `certificates/` from `~/.purple` into `~/.qtpurple` while neither program is running. libpurple stores the enabled flag per UI, so the copied accounts start out disabled; enable them in **Accounts → Manage Accounts**.

Additional plugins are loaded from `<config dir>/plugins` as well as libpurple's system plugin directory.

### Keyboard

| Key | Action |
|---|---|
| Enter / Shift+Enter | Send / new line |
| Ctrl+Up / Ctrl+Down | Previous / next sent message |
| Tab | Complete nickname (chats) |
| Ctrl+W | Close conversation tab |
| Ctrl+PgUp / Ctrl+PgDn | Previous / next tab |
| Ctrl+M / Ctrl+C / Ctrl+I | New IM / Join chat / Get user info |
| Ctrl+B | Add buddy |
| Ctrl+A / Ctrl+U / Ctrl+T | Accounts / Plugins / File transfers |
| Ctrl+P | Preferences |
| Ctrl+S | Mute sounds |
| Ctrl+Q | Quit |

## Source layout

| File | Purpose |
|---|---|
| `src/main.cpp` | Command line, QApplication, start-up/shutdown |
| `src/core.*` | libpurple init/quit, core/account/connection UI ops, reconnect logic |
| `src/eventloop.*` | `PurpleEventLoopUiOps` on QTimer/QSocketNotifier |
| `src/blistwindow.*` | Buddy list window, `PurpleBlistUiOps`, status selector, menus |
| `src/blistdialogs.*` | Account selector, Add Buddy, Add/Join Chat, New IM / Get Info |
| `src/accountsdialog.*` | Account manager and editor |
| `src/conversationwindow.*` | Tabbed conversations, `PurpleConversationUiOps` |
| `src/requestui.*` | `PurpleRequestUiOps` |
| `src/notifyui.*` | `PurpleNotifyUiOps` |
| `src/transferswindow.*` | `PurpleXferUiOps` |
| `src/pluginsdialog.*` | Plugin manager and plugin preference editor |
| `src/debugwindow.*` | `PurpleDebugUiOps` |
| `src/sound.*` | `PurpleSoundUiOps`, sound event hooks, playback |
| `src/idle.*`, `src/idle_x11.*` | `PurpleIdleUiOps` and idle backends |
| `src/pounces.*` | Pounce handler, manager, editor |
| `src/trayicon.*` | System tray icon and menu |
| `src/prefsdialog.*` | Preferences dialog |
| `src/emoji.*` | Emoji `:code:` conversion, blocked-image fallback, reaction detection |

## Design notes

- **Event loop:** libpurple timeouts and socket watches are implemented with `QTimer` and a `QSocketNotifier` subclass, so Qt doesn't need to be built with the GLib event dispatcher. Timeouts may be added or removed from libpurple helper threads (the Win32 DNS resolver does this); the Qt objects themselves are always created and destroyed on the GUI thread. Callbacks never run while the internal lock is held, so a callback can remove its own source.
- **Pointer lifetime:** buddy-list tree items only point at live `PurpleBlistNode`s. When libpurple removes a displayed node, the tree is cleared inside the remove callback, before the node is freed, and a rebuild is scheduled. Every deferred action (menus, dialogs) re-checks that its node, account, connection, or conversation still exists before using it.
- **Request/notify windows:** libpurple callbacks run exactly once. When libpurple closes a request itself, the window just disappears and no callback runs. Closing an *action* request window chooses the last action, because libpurple callers put the negative option ("Cancel", "No", "Reject") last; running no callback at all would leave prompts such as certificate verification hanging.
- **Untrusted content:** message HTML is shown in a `QTextBrowser` that never loads remote or local resources (only images registered from libpurple's image store). Only `http`, `https`, `ftp`, `mailto`, `xmpp`, `irc`, and `ircs` links are opened. `file:` links only reveal the containing folder and never launch the file.
- **Sounds:** libpurple never decides on its own that a sound should play; the UI hooks the conversation/buddy signals and calls `purple_sound_play_event()`, which applies libpurple's "sounds while" rule and a 2-second repeat limit before calling back into `play_event`. Custom sound commands are split like a shell command line but run directly (no shell); `%s` becomes the file name.
- **Pounces:** action names and attributes match Pidgin's (`open-window`, `popup-notify`, `send-message`, `execute-command`, `play-sound`). libpurple stores the owning UI with each pounce, so pounces created in Pidgin are not run by QtPurple. The pounce handler is registered before `pounces.xml` is loaded so loaded pounces get their actions.
- **Idle:** the GNOME and X11 queries are synchronous but short (D-Bus timeout 250 ms) and libpurple only polls every few seconds. XScreenSaver is not used under XWayland, where it only sees input sent to X clients. If a backend stops answering, QtPurple falls back to its own window activity.
- **Emoji and reactions:** remote `<img>` tags are still never fetched; they are replaced by their `alt`/`title` text. `:code:` conversion skips tags and link text. A message counts as a reaction when its plain text starts with "reacted with" or "reacted to" (up to four leading words, e.g. a name, are allowed), which matches notices such as "Reacted with a :rainbowsmile:" — protocols that word reactions differently are not detected. The view keeps the last 3000 messages so it can be redrawn when an option is switched. Per-conversation choices are stored on the buddy or chat entry (`qtpurple-show-reactions`, `qtpurple-emoji-codes` in `blist.xml`); conversations with people not on the buddy list use the defaults. Hiding reactions only affects the display: sounds, pounces and logs are unchanged.
- **Tray:** the menu is rebuilt only when it is closed and never from inside one of its own actions; blinking only swaps the icon.
- `QT_NO_KEYWORDS` is defined because GLib headers use `signals` as an identifier.

## Not implemented yet

Showing a protocol's own emoji images (they would have to be downloaded), conversation log viewer, room-list browser, privacy/block-list editor, a full preferences dialog (only sounds, away/idle, and tray settings exist), sound themes, choosing the saved status used for idle-away, per-account proxy settings, custom smileys, voice/video, spell checking, and a rich-text formatting toolbar.

Idle limitation: on Wayland desktops other than GNOME (e.g. KDE Plasma on Wayland) no system idle source is used, so only activity in QtPurple's windows counts.

## Verification status

- Every libpurple/`serv_*` function, enum, and macro used in the sources was checked against the libpurple 2.14.14 headers: all identifiers exist and all call argument counts match the prototypes.
- Every UI-ops callback (including `PurpleSoundUiOps` and `PurpleIdleUiOps`) and the pounce handler callbacks were compiled against the libpurple headers to confirm their exact function types. Signal callback parameter lists were compared by hand with the marshallers libpurple registers.
- `src/idle_x11.cpp` compiles against Xlib with a stand-in for the libXss header (the real header was not available).
- `purple.h` compiles cleanly as C++17 with g++.
- **The full application has not been compiled yet.** Qt development headers were not available in the environment where the code was written, so the first build may need small fixes.

### OS/2

The qmake project builds without pkg-config (`PURPLE_PREFIX` / `GLIB_PREFIX`). This setup is untested, and two things haven't been checked: whether a libpurple/GLib port is available for the target, and whether the OS/2 Qt 5 `QSocketNotifier` handles the pipe descriptors used by libpurple's forked DNS resolver.

## License

GPL-2.0-or-later, matching libpurple.
