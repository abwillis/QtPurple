/*
 * QtPurple - a Qt front end for libpurple
 *
 * Sounds: PurpleSoundUiOps and event hooks.
 */
#include "sound.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QHash>
#include <QStandardPaths>
#include <QUrl>
#include <QVector>

#ifdef QTPURPLE_HAVE_QTMULTIMEDIA
#include <QSoundEffect>
#endif

#include <cstring>
#include <utility>

namespace {

const Sound::EventInfo kEvents[] = {
    { PURPLE_SOUND_BUDDY_ARRIVE,   "login",          QT_TRANSLATE_NOOP("Sound", "Buddy logs in"),                  "login.wav",   true  },
    { PURPLE_SOUND_BUDDY_LEAVE,    "logout",         QT_TRANSLATE_NOOP("Sound", "Buddy logs out"),                 "logout.wav",  true  },
    { PURPLE_SOUND_RECEIVE,        "im_recv",        QT_TRANSLATE_NOOP("Sound", "Message received"),               "receive.wav", true  },
    { PURPLE_SOUND_FIRST_RECEIVE,  "first_im_recv",  QT_TRANSLATE_NOOP("Sound", "Message received begins conversation"), "alert.wav", false },
    { PURPLE_SOUND_SEND,           "send_im",        QT_TRANSLATE_NOOP("Sound", "Message sent"),                   "send.wav",    true  },
    { PURPLE_SOUND_CHAT_JOIN,      "join_chat",      QT_TRANSLATE_NOOP("Sound", "Person enters chat"),             "login.wav",   false },
    { PURPLE_SOUND_CHAT_LEAVE,     "left_chat",      QT_TRANSLATE_NOOP("Sound", "Person leaves chat"),             "logout.wav",  false },
    { PURPLE_SOUND_CHAT_YOU_SAY,   "send_chat_msg",  QT_TRANSLATE_NOOP("Sound", "You talk in chat"),               "send.wav",    false },
    { PURPLE_SOUND_CHAT_SAY,       "chat_msg_recv",  QT_TRANSLATE_NOOP("Sound", "Others talk in chat"),            "receive.wav", false },
    { PURPLE_SOUND_POUNCE_DEFAULT, "pounce_default", QT_TRANSLATE_NOOP("Sound", "Default buddy pounce sound"),     "alert.wav",   true  },
    { PURPLE_SOUND_CHAT_NICK,      "nick_said",      QT_TRANSLATE_NOOP("Sound", "Someone says your name in chat"), "alert.wav",   false },
    { PURPLE_SOUND_GOT_ATTENTION,  "attention",      QT_TRANSLATE_NOOP("Sound", "Attention received"),             "alert.wav",   true  },
};
const int kEventCount = int(sizeof(kEvents) / sizeof(kEvents[0]));

int g_signalHandle = 0;
qint64 g_loginMuteUntil = 0;        /* ms since epoch */
bool g_eventReached = false;        /* set by uiPlayEvent, see requestEvent() */
const qint64 kLoginMuteMs = 10000;  /* like Pidgin: no arrive/leave flood at sign-on */

const Sound::EventInfo *findEvent(PurpleSoundEventID id)
{
    for (int i = 0; i < kEventCount; ++i) {
        if (kEvents[i].id == id)
            return &kEvents[i];
    }
    return nullptr;
}

QString fromGlibFileName(const char *name)
{
#ifdef _WIN32
    return qs(name);
#else
    return name ? QFile::decodeName(name) : QString();
#endif
}

QByteArray toGlibFileName(const QString &name)
{
#ifdef _WIN32
    return name.toUtf8();
#else
    return QFile::encodeName(name);
#endif
}

/* ----------------------------------------------------------- playback */

/* Run argv (UTF-8 / filename encoded). Returns false if it could not start. */
bool spawn(const QList<QByteArray> &args)
{
    if (args.isEmpty())
        return false;
    QVector<char *> argv;
    for (const QByteArray &a : args)
        argv.append(const_cast<char *>(a.constData()));
    argv.append(nullptr);

    GError *error = nullptr;
    const gboolean ok = g_spawn_async(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH,
                                      nullptr, nullptr, nullptr, &error);
    if (!ok) {
        purple_debug_error("sound", "Could not run %s: %s\n", argv.first(),
                           error ? error->message : "unknown error");
        if (error)
            g_error_free(error);
    }
    return ok;
}

/* User command: split like a shell (no shell is run), substitute %s. */
bool playWithCommand(const QString &command, const QString &file)
{
    const QByteArray cmd = command.trimmed().toUtf8();
    if (cmd.isEmpty())
        return false;

    gint argc = 0;
    gchar **parsed = nullptr;
    GError *error = nullptr;
    if (!g_shell_parse_argv(cmd.constData(), &argc, &parsed, &error)) {
        purple_debug_error("sound", "Bad sound command: %s\n", error ? error->message : "");
        if (error)
            g_error_free(error);
        return false;
    }

    const QByteArray fileArg = toGlibFileName(file);
    QList<QByteArray> args;
    bool substituted = false;
    for (gint i = 0; i < argc; ++i) {
        QByteArray a(parsed[i]);
        if (a.contains("%s")) {
            a.replace("%s", fileArg);
            substituted = true;
        }
        args.append(a);
    }
    g_strfreev(parsed);
    if (!substituted)
        args.append(fileArg);
    return spawn(args);
}

bool playWithInstalledPlayer(const QString &file)
{
    static const char *players[] = { "pw-play", "paplay", "aplay" };
    for (const char *p : players) {
        if (strcmp(p, "aplay") == 0 && !file.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive))
            continue;           /* aplay only plays WAV/AU/VOC/raw */
        gchar *path = g_find_program_in_path(p);
        if (!path)
            continue;
        g_free(path);
        return spawn({ QByteArray(p), toGlibFileName(file) });
    }
    return false;
}

#ifdef QTPURPLE_HAVE_QTMULTIMEDIA
QHash<QString, QSoundEffect *> g_effects;

void clearEffects()
{
    for (QSoundEffect *e : std::as_const(g_effects))
        e->deleteLater();
    g_effects.clear();
}

void playWithQt(const QString &file)
{
    QSoundEffect *effect = g_effects.value(file, nullptr);
    if (!effect) {
        if (g_effects.size() >= 16)
            clearEffects();
        effect = new QSoundEffect(qApp);
        effect->setSource(QUrl::fromLocalFile(file));
        g_effects.insert(file, effect);
    }
    if (effect->status() == QSoundEffect::Error) {
        /* Qt Multimedia could not decode/open it: try an external player. */
        purple_debug_warning("sound", "Qt Multimedia cannot play %s; trying an external player\n",
                             file.toUtf8().constData());
        g_effects.remove(file);
        effect->deleteLater();
        if (!playWithInstalledPlayer(file))
            QApplication::beep();
        return;
    }
    effect->setVolume(qBound(0, purple_prefs_get_int(QTPURPLE_PREF_SOUND_VOLUME), 100) / 100.0);
    effect->play();         /* queued until loaded if necessary */
}
#endif

void playFile(const QString &file)
{
    const QString method = qs(purple_prefs_get_string(QTPURPLE_PREF_SOUND_METHOD));
    if (method == QLatin1String("none"))
        return;
    if (method == QLatin1String("beep") || file.isEmpty() || !QFileInfo::exists(file)) {
        if (method != QLatin1String("beep") && !file.isEmpty())
            purple_debug_warning("sound", "Sound file not found: %s\n", file.toUtf8().constData());
        QApplication::beep();
        return;
    }
    if (method == QLatin1String("command")) {
        if (!playWithCommand(qs(purple_prefs_get_string(QTPURPLE_PREF_SOUND_COMMAND)), file))
            QApplication::beep();
        return;
    }

    /* automatic */
#ifdef QTPURPLE_HAVE_QTMULTIMEDIA
    if (file.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive)) {
        playWithQt(file);
        return;
    }
#endif
    if (!playWithInstalledPlayer(file))
        QApplication::beep();
}

/* --------------------------------------------------------------- ui ops */

void uiPlayFile(const char *filename)
{
    if (Sound::muted())
        return;
    playFile(fromGlibFileName(filename));
}

void uiPlayEvent(PurpleSoundEventID id)
{
    g_eventReached = true;
    if (Sound::muted())
        return;
    const Sound::EventInfo *e = findEvent(id);
    if (!e)
        return;
    if (!purple_prefs_get_bool(Utf8(Sound::enabledPref(*e))))
        return;
    const QString file = Sound::effectiveFile(*e);
    purple_debug_info("sound", "Playing '%s': %s (method %s)\n", e->pref,
                      file.isEmpty() ? "(no file)" : file.toUtf8().constData(),
                      purple_prefs_get_string(QTPURPLE_PREF_SOUND_METHOD));
    playFile(file);
}

/* --------------------------------------------------------- event hooks */

bool eventEnabled(PurpleSoundEventID id);

const char *eventName(PurpleSoundEventID id)
{
    const Sound::EventInfo *e = findEvent(id);
    return e ? e->pref : "unknown";
}

/* libpurple's "sounds while" rule (purple_sound_play_required()), repeated
 * here only to say in the debug log why an event stays silent. */
bool statusAllows(PurpleAccount *account, QByteArray *why)
{
    const int rule = purple_prefs_get_int("/purple/sound/while_status");
    if (rule == 3 || !account)
        return true;
    PurpleStatus *status = purple_account_get_active_status(account);
    if (!status || !purple_status_is_online(status))
        return true;
    const bool available = purple_status_is_available(status);
    const bool ok = (available && rule == 1) || (!available && rule == 2);
    if (!ok) {
        const char *name = purple_status_get_name(status);
        *why = QByteArray("account status is '") + (name ? name : "?") + "' and Sounds while is '"
               + (rule == 1 ? "Only when available" : rule == 2 ? "Only when not available" : "?") + "'";
    }
    return ok;
}

/* Every UI sound event goes through here: the reason an event is not
 * played is written to the debug log (Tools > Debug Window, or -d). */
void requestEvent(PurpleSoundEventID id, PurpleAccount *account, PurpleConversation *conv)
{
    const char *name = eventName(id);
    if (Sound::muted()) {
        purple_debug_info("sound", "'%s' not played: sounds are muted\n", name);
        return;
    }
    if (!eventEnabled(id)) {
        purple_debug_info("sound", "'%s' not played: the event is switched off in Preferences\n", name);
        return;
    }
    if (conv && !purple_prefs_get_bool(QTPURPLE_PREF_SOUND_FOCUS) && purple_conversation_has_focus(conv)) {
        purple_debug_info("sound", "'%s' not played: the conversation has focus\n", name);
        return;
    }
    QByteArray why;
    if (!statusAllows(account, &why)) {
        purple_debug_info("sound", "'%s' not played: %s\n", name, why.constData());
        return;
    }
    g_eventReached = false;
    purple_sound_play_event(id, account);
    if (!g_eventReached)
        purple_debug_info("sound", "'%s' not played: suppressed by libpurple "
                          "(same sound within 2 seconds, or a plugin)\n", name);
}

bool loginMuted()
{
    return QDateTime::currentMSecsSinceEpoch() < g_loginMuteUntil;
}

bool eventEnabled(PurpleSoundEventID id)
{
    const Sound::EventInfo *e = findEvent(id);
    return e && purple_prefs_get_bool(Utf8(Sound::enabledPref(*e)));
}

/* Like Pidgin: optionally stay quiet for the conversation being read. */
void playConvEvent(PurpleConversation *conv, PurpleSoundEventID id)
{
    requestEvent(id, conv ? purple_conversation_get_account(conv) : nullptr, conv);
}

void onAccountSignedOn(PurpleAccount *, gpointer)
{
    g_loginMuteUntil = QDateTime::currentMSecsSinceEpoch() + kLoginMuteMs;
}

void onBuddySignedOn(PurpleBuddy *buddy, gpointer)
{
    if (!loginMuted())
        requestEvent(PURPLE_SOUND_BUDDY_ARRIVE, purple_buddy_get_account(buddy), nullptr);
}

void onBuddySignedOff(PurpleBuddy *buddy, gpointer)
{
    if (!loginMuted())
        requestEvent(PURPLE_SOUND_BUDDY_LEAVE, purple_buddy_get_account(buddy), nullptr);
}

void onReceivedIm(PurpleAccount *account, char *, char *, PurpleConversation *conv,
                  PurpleMessageFlags flags, gpointer)
{
    if (flags & PURPLE_MESSAGE_DELAYED) {
        purple_debug_info("sound", "'im_recv' not played: delayed (offline/history) message\n");
        return;
    }
    if (!conv) {
        /* No window yet: this message starts a conversation. */
        requestEvent(eventEnabled(PURPLE_SOUND_FIRST_RECEIVE)
                         ? PURPLE_SOUND_FIRST_RECEIVE : PURPLE_SOUND_RECEIVE,
                     account, nullptr);
        return;
    }
    playConvEvent(conv, PURPLE_SOUND_RECEIVE);
}

void onSentIm(PurpleAccount *account, const char *receiver, const char *, gpointer)
{
    PurpleConversation *conv =
        purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, receiver, account);
    playConvEvent(conv, PURPLE_SOUND_SEND);
}

void onReceivedChat(PurpleAccount *, char *sender, char *, PurpleConversation *conv,
                    PurpleMessageFlags flags, gpointer)
{
    if (flags & PURPLE_MESSAGE_DELAYED)
        return;
    if (!conv)
        return;
    PurpleConvChat *chat = PURPLE_CONV_CHAT(conv);
    const char *nick = purple_conv_chat_get_nick(chat);
    if (sender && nick && purple_utf8_strcasecmp(sender, nick) == 0)
        return;     /* our own message echoed back */
    if (sender && purple_conv_chat_is_user_ignored(chat, sender))
        return;

    if ((flags & PURPLE_MESSAGE_NICK) && eventEnabled(PURPLE_SOUND_CHAT_NICK))
        playConvEvent(conv, PURPLE_SOUND_CHAT_NICK);
    else
        playConvEvent(conv, PURPLE_SOUND_CHAT_SAY);
}

void onSentChat(PurpleAccount *account, const char *, int id, gpointer)
{
    PurpleConnection *gc = purple_account_get_connection(account);
    PurpleConversation *conv = gc ? purple_find_chat(gc, id) : nullptr;
    playConvEvent(conv, PURPLE_SOUND_CHAT_YOU_SAY);
}

void onChatBuddyJoined(PurpleConversation *conv, const char *, PurpleConvChatBuddyFlags,
                       gboolean newArrival, gpointer)
{
    if (newArrival)
        playConvEvent(conv, PURPLE_SOUND_CHAT_JOIN);
}

void onChatBuddyLeft(PurpleConversation *conv, const char *, const char *, gpointer)
{
    playConvEvent(conv, PURPLE_SOUND_CHAT_LEAVE);
}

void onGotAttention(PurpleAccount *account, const char *, PurpleConversation *conv,
                    guint, gpointer)
{
    if (conv)
        playConvEvent(conv, PURPLE_SOUND_GOT_ATTENTION);
    else
        requestEvent(PURPLE_SOUND_GOT_ATTENTION, account, nullptr);
}

void uiInit()
{
    void *accounts = purple_accounts_get_handle();
    void *blist = purple_blist_get_handle();
    void *convs = purple_conversations_get_handle();

    purple_signal_connect(accounts, "account-signed-on", &g_signalHandle,
                          PURPLE_CALLBACK(onAccountSignedOn), nullptr);
    purple_signal_connect(blist, "buddy-signed-on", &g_signalHandle,
                          PURPLE_CALLBACK(onBuddySignedOn), nullptr);
    purple_signal_connect(blist, "buddy-signed-off", &g_signalHandle,
                          PURPLE_CALLBACK(onBuddySignedOff), nullptr);
    purple_signal_connect(convs, "received-im-msg", &g_signalHandle,
                          PURPLE_CALLBACK(onReceivedIm), nullptr);
    purple_signal_connect(convs, "sent-im-msg", &g_signalHandle,
                          PURPLE_CALLBACK(onSentIm), nullptr);
    purple_signal_connect(convs, "received-chat-msg", &g_signalHandle,
                          PURPLE_CALLBACK(onReceivedChat), nullptr);
    purple_signal_connect(convs, "sent-chat-msg", &g_signalHandle,
                          PURPLE_CALLBACK(onSentChat), nullptr);
    purple_signal_connect(convs, "chat-buddy-joined", &g_signalHandle,
                          PURPLE_CALLBACK(onChatBuddyJoined), nullptr);
    purple_signal_connect(convs, "chat-buddy-left", &g_signalHandle,
                          PURPLE_CALLBACK(onChatBuddyLeft), nullptr);
    purple_signal_connect(convs, "got-attention", &g_signalHandle,
                          PURPLE_CALLBACK(onGotAttention), nullptr);
}

void uiUninit()
{
    purple_signals_disconnect_by_handle(&g_signalHandle);
#ifdef QTPURPLE_HAVE_QTMULTIMEDIA
    clearEffects();
#endif
}

} // namespace

namespace Sound {

const EventInfo *events(int *count)
{
    if (count)
        *count = kEventCount;
    return kEvents;
}

QString enabledPref(const EventInfo &e)
{
    return QStringLiteral(QTPURPLE_PREF_SOUND_ENABLED "/%1").arg(QLatin1String(e.pref));
}

QString filePref(const EventInfo &e)
{
    return QStringLiteral(QTPURPLE_PREF_SOUND_FILE "/%1").arg(QLatin1String(e.pref));
}

void addPrefs()
{
    purple_prefs_add_none(QTPURPLE_PREF_SOUND);
    purple_prefs_add_none(QTPURPLE_PREF_SOUND_ENABLED);
    purple_prefs_add_none(QTPURPLE_PREF_SOUND_FILE);
    purple_prefs_add_string(QTPURPLE_PREF_SOUND_METHOD, "automatic");
    purple_prefs_add_string(QTPURPLE_PREF_SOUND_COMMAND, "");
    purple_prefs_add_int(QTPURPLE_PREF_SOUND_VOLUME, 50);
    purple_prefs_add_bool(QTPURPLE_PREF_SOUND_MUTE, FALSE);
    purple_prefs_add_bool(QTPURPLE_PREF_SOUND_FOCUS, TRUE);
    for (int i = 0; i < kEventCount; ++i) {
        purple_prefs_add_bool(Utf8(enabledPref(kEvents[i])), kEvents[i].defaultEnabled ? TRUE : FALSE);
        purple_prefs_add_path(Utf8(filePref(kEvents[i])), "");
    }
}

PurpleSoundUiOps *uiOps()
{
    static PurpleSoundUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.init = uiInit;
        ops.uninit = uiUninit;
        ops.play_file = uiPlayFile;
        ops.play_event = uiPlayEvent;
        ready = true;
    }
    return &ops;
}

QString defaultFile(const EventInfo &e)
{
    /* Pidgin installs its sounds as <datadir>/sounds/purple/<file>. */
    const QString rel = QStringLiteral("sounds/purple/%1").arg(QLatin1String(e.defaultFile));
    QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, rel);
    if (path.isEmpty()) {
        /* Also accept <config dir>/sounds/<file>. */
        const QString local = fromGlibFileName(purple_user_dir()) + QStringLiteral("/sounds/")
                              + QLatin1String(e.defaultFile);
        if (QFileInfo::exists(local))
            path = local;
    }
    return path;
}

QString effectiveFile(const EventInfo &e)
{
    const QString custom = fromGlibFileName(purple_prefs_get_path(Utf8(filePref(e))));
    return custom.isEmpty() ? defaultFile(e) : custom;
}

void test(const QString &file)
{
    playFile(file);
}

bool muted()
{
    return purple_prefs_get_bool(QTPURPLE_PREF_SOUND_MUTE);
}

void setMuted(bool mute)
{
    purple_prefs_set_bool(QTPURPLE_PREF_SOUND_MUTE, mute ? TRUE : FALSE);
}

bool haveQtMultimedia()
{
#ifdef QTPURPLE_HAVE_QTMULTIMEDIA
    return true;
#else
    return false;
#endif
}

} // namespace Sound
