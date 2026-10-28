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

**libpurple request/notify UI**
- Input, choice, action, multi-field forms (string, integer, boolean, choice, list, label, image, account), file and folder dialogs
- Messages, e-mail notifications, formatted text, user info, search results with protocol buttons, URIs

**Other**
- File transfer window (progress, cancel, open containing folder)
- Plugin manager with a generic preference editor for plugins that provide a preference frame
- Debug window fed by libpurple's debug output

## Building

### openSUSE Tumbleweed

```bash
sudo zypper install libpurple-devel glib2-devel cmake gcc-c++ qt6-base-devel
# or, for Qt 5:  libqt5-qtbase-devel
```

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

## Design notes

- **Event loop:** libpurple timeouts and socket watches are implemented with `QTimer` and a `QSocketNotifier` subclass, so Qt doesn't need to be built with the GLib event dispatcher. Timeouts may be added or removed from libpurple helper threads (the Win32 DNS resolver does this); the Qt objects themselves are always created and destroyed on the GUI thread. Callbacks never run while the internal lock is held, so a callback can remove its own source.
- **Pointer lifetime:** buddy-list tree items only point at live `PurpleBlistNode`s. When libpurple removes a displayed node, the tree is cleared inside the remove callback, before the node is freed, and a rebuild is scheduled. Every deferred action (menus, dialogs) re-checks that its node, account, connection, or conversation still exists before using it.
- **Request/notify windows:** libpurple callbacks run exactly once. When libpurple closes a request itself, the window just disappears and no callback runs. Closing an *action* request window chooses the last action, because libpurple callers put the negative option ("Cancel", "No", "Reject") last; running no callback at all would leave prompts such as certificate verification hanging.
- **Untrusted content:** message HTML is shown in a `QTextBrowser` that never loads remote or local resources (only images registered from libpurple's image store). Only `http`, `https`, `ftp`, `mailto`, `xmpp`, `irc`, and `ircs` links are opened. `file:` links only reveal the containing folder and never launch the file.
- `QT_NO_KEYWORDS` is defined because GLib headers use `signals` as an identifier.

## Not implemented yet

Buddy pounces UI, conversation log viewer, room-list browser, privacy/block-list editor, sounds, a full preferences dialog, per-account proxy settings, custom smileys, voice/video, system tray icon, spell checking, a rich-text formatting toolbar, and system idle detection (`PurpleIdleUiOps`).

## Verification status

- Every libpurple/`serv_*` function, enum, and macro used in the sources was checked against the libpurple 2.14.14 headers: all identifiers exist and all call argument counts match the prototypes.
- `purple.h` compiles cleanly as C++17 with g++.
- **The full application has not been compiled yet.** Qt development headers were not available in the environment where the code was written, so the first build may need small fixes.

### OS/2

The qmake project builds without pkg-config (`PURPLE_PREFIX` / `GLIB_PREFIX`). This setup is untested, and two things haven't been checked: whether a libpurple/GLib port is available for the target, and whether the OS/2 Qt 5 `QSocketNotifier` handles the pipe descriptors used by libpurple's forked DNS resolver.

## License

GPL-2.0-or-later, matching libpurple.
