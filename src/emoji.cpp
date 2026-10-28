/*
 * QtPurple - a Qt front end for libpurple
 *
 * Emoji and reaction handling for the conversation view.
 */
#include "emoji.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

namespace {

/* Common shortcodes (Slack/GitHub/Teams style names). Codes that have no
 * Unicode equivalent (custom service emoticons) belong in emoji.txt. */
const struct { const char *code; const char *glyph; } kBuiltin[] = {
    { "smile", "\U0001F642" },          { "slightly_smiling_face", "\U0001F642" },
    { "smiley", "\U0001F603" },         { "grin", "\U0001F601" },
    { "grinning", "\U0001F600" },       { "laugh", "\U0001F606" },
    { "laughing", "\U0001F606" },       { "joy", "\U0001F602" },
    { "rofl", "\U0001F923" },           { "wink", "\U0001F609" },
    { "blush", "\U0001F60A" },          { "innocent", "\U0001F607" },
    { "heart_eyes", "\U0001F60D" },     { "kiss", "\U0001F618" },
    { "kissing_heart", "\U0001F618" },  { "yum", "\U0001F60B" },
    { "stuck_out_tongue", "\U0001F61B" }, { "tongueout", "\U0001F61B" },
    { "sunglasses", "\U0001F60E" },     { "cool", "\U0001F60E" },
    { "thinking", "\U0001F914" },       { "neutral_face", "\U0001F610" },
    { "expressionless", "\U0001F611" }, { "unamused", "\U0001F612" },
    { "rolling_eyes", "\U0001F644" },   { "smirk", "\U0001F60F" },
    { "sad", "\U0001F641" },            { "slightly_frowning_face", "\U0001F641" },
    { "disappointed", "\U0001F61E" },   { "worried", "\U0001F61F" },
    { "cry", "\U0001F622" },            { "sob", "\U0001F62D" },
    { "angry", "\U0001F620" },          { "rage", "\U0001F621" },
    { "surprised", "\U0001F62E" },      { "open_mouth", "\U0001F62E" },
    { "astonished", "\U0001F632" },     { "scream", "\U0001F631" },
    { "flushed", "\U0001F633" },        { "sweat_smile", "\U0001F605" },
    { "sleeping", "\U0001F634" },       { "sleepy", "\U0001F62A" },
    { "mask", "\U0001F637" },           { "nerd_face", "\U0001F913" },
    { "upside_down_face", "\U0001F643" }, { "hugs", "\U0001F917" },
    { "hug", "\U0001F917" },            { "facepalm", "\U0001F926" },
    { "shrug", "\U0001F937" },          { "party", "\U0001F973" },
    { "partying_face", "\U0001F973" },  { "zany_face", "\U0001F92A" },
    { "heart", "\u2764\uFE0F" },        { "broken_heart", "\U0001F494" },
    { "sparkling_heart", "\U0001F496" }, { "two_hearts", "\U0001F495" },
    { "like", "\U0001F44D" },           { "yes", "\U0001F44D" },
    { "+1", "\U0001F44D" },             { "thumbsup", "\U0001F44D" },
    { "-1", "\U0001F44E" },             { "thumbsdown", "\U0001F44E" },
    { "no", "\U0001F44E" },             { "ok_hand", "\U0001F44C" },
    { "clap", "\U0001F44F" },           { "wave", "\U0001F44B" },
    { "pray", "\U0001F64F" },           { "muscle", "\U0001F4AA" },
    { "raised_hands", "\U0001F64C" },   { "point_up", "\u261D\uFE0F" },
    { "v", "\u270C\uFE0F" },            { "handshake", "\U0001F91D" },
    { "eyes", "\U0001F440" },           { "fire", "\U0001F525" },
    { "100", "\U0001F4AF" },            { "tada", "\U0001F389" },
    { "star", "\u2B50" },               { "sparkles", "\u2728" },
    { "rainbow", "\U0001F308" },        { "sun", "\u2600\uFE0F" },
    { "sunny", "\u2600\uFE0F" },        { "cloud", "\u2601\uFE0F" },
    { "zap", "\u26A1" },                { "snowflake", "\u2744\uFE0F" },
    { "coffee", "\u2615" },             { "beer", "\U0001F37A" },
    { "beers", "\U0001F37B" },          { "pizza", "\U0001F355" },
    { "cake", "\U0001F370" },           { "gift", "\U0001F381" },
    { "rocket", "\U0001F680" },         { "bulb", "\U0001F4A1" },
    { "check", "\u2714\uFE0F" },        { "heavy_check_mark", "\u2714\uFE0F" },
    { "white_check_mark", "\u2705" },   { "x", "\u274C" },
    { "warning", "\u26A0\uFE0F" },      { "question", "\u2753" },
    { "exclamation", "\u2757" },        { "skull", "\U0001F480" },
    { "poop", "\U0001F4A9" },           { "ghost", "\U0001F47B" },
    { "robot", "\U0001F916" },          { "see_no_evil", "\U0001F648" },
    { "dog", "\U0001F436" },            { "cat", "\U0001F431" },
};

QHash<QString, QString> g_builtin;
QHash<QString, QString> g_user;
QString g_userPath;
QDateTime g_userStamp;
qint64 g_userSize = -1;

void ensureBuiltin()
{
    if (!g_builtin.isEmpty())
        return;
    for (const auto &e : kBuiltin)
        g_builtin.insert(QString::fromUtf8(e.code), QString::fromUtf8(e.glyph));
}

/* Re-read emoji.txt when it was created, changed or removed. */
void refreshUserMap()
{
    const QString path = Emoji::userMapPath();
    const QFileInfo fi(path);
    const bool exists = fi.exists();
    const QDateTime stamp = exists ? fi.lastModified() : QDateTime();
    const qint64 size = exists ? fi.size() : -1;
    if (path == g_userPath && stamp == g_userStamp && size == g_userSize)
        return;
    g_userPath = path;
    g_userStamp = stamp;
    g_userSize = size;
    g_user.clear();
    if (!exists)
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream in(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        QString code = line.left(eq).trimmed();
        if (code.size() > 2 && code.startsWith(QLatin1Char(':')) && code.endsWith(QLatin1Char(':')))
            code = code.mid(1, code.size() - 2);
        const QString value = line.mid(eq + 1).trimmed();
        if (!code.isEmpty() && !value.isEmpty())
            g_user.insert(code.toLower(), value);
    }
}

QString lookup(const QString &code)
{
    const QString key = code.toLower();
    auto u = g_user.constFind(key);
    if (u != g_user.constEnd())
        return u.value();
    auto b = g_builtin.constFind(key);
    if (b != g_builtin.constEnd())
        return b.value();
    return QString();
}

QString attribute(const QString &tag, const QString &name)
{
    const QRegularExpression re(
        QStringLiteral("\\b%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(name),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(tag);
    if (!m.hasMatch())
        return QString();
    for (int i = 1; i <= 3; ++i) {
        if (m.capturedStart(i) >= 0)
            return m.captured(i);
    }
    return QString();
}

/* <img> tags that are not one of our registered purpleimg: resources
 * would render as nothing; show their alt/title text instead. */
QString replaceBlockedImages(const QString &html)
{
    static const QRegularExpression imgRe(QStringLiteral("<img\\b[^>]*>"),
                                          QRegularExpression::CaseInsensitiveOption);
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = imgRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();

        const QString tag = m.captured(0);
        if (attribute(tag, QStringLiteral("src")).startsWith(QLatin1String("purpleimg:"), Qt::CaseInsensitive)) {
            out += tag;
            continue;
        }
        QString text = attribute(tag, QStringLiteral("alt"));
        if (text.trimmed().isEmpty())
            text = attribute(tag, QStringLiteral("title"));
        /* Attribute values are already HTML text; drop anything tag-like. */
        text.remove(QLatin1Char('<'));
        text.remove(QLatin1Char('>'));
        out += text.trimmed().isEmpty() ? QStringLiteral("[image]") : text;
    }
    out += html.mid(last);
    return out;
}

/* ":code:" -> emoji in text outside of tags and outside of <a> elements. */
QString convertCodes(const QString &html)
{
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    static const QRegularExpression codeRe(QStringLiteral(":([A-Za-z0-9_+\\-]{1,48}):"));
    static const QRegularExpression aOpen(QStringLiteral("^<a\\b"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression aClose(QStringLiteral("^</a\\s*>"), QRegularExpression::CaseInsensitiveOption);

    auto convertText = [](const QString &text) {
        QString out;
        int last = 0;
        int from = 0;
        for (;;) {
            const QRegularExpressionMatch m = codeRe.match(text, from);
            if (!m.hasMatch())
                break;
            const QString glyph = lookup(m.captured(1));
            if (glyph.isEmpty()) {
                /* Unknown: the closing ':' may open the next code ("a :x:y:"). */
                from = m.capturedEnd() - 1;
                continue;
            }
            out += text.mid(last, m.capturedStart() - last);
            out += QStringLiteral("<span title=\"%1\">%2</span>")
                       .arg(m.captured(0).toHtmlEscaped(), glyph.toHtmlEscaped());
            last = from = m.capturedEnd();
        }
        out += text.mid(last);
        return out;
    };

    QString out;
    int last = 0;
    int inLink = 0;
    QRegularExpressionMatchIterator it = tagRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString text = html.mid(last, m.capturedStart() - last);
        out += inLink ? text : convertText(text);
        const QString tag = m.captured(0);
        if (aOpen.match(tag).hasMatch())
            ++inLink;
        else if (aClose.match(tag).hasMatch() && inLink > 0)
            --inLink;
        out += tag;
        last = m.capturedEnd();
    }
    const QString tail = html.mid(last);
    out += inLink ? tail : convertText(tail);
    return out;
}

} // namespace

namespace Emoji {

void addPrefs()
{
    purple_prefs_add_bool(QTPURPLE_PREF_SHOW_REACTIONS, TRUE);
    purple_prefs_add_bool(QTPURPLE_PREF_EMOJI_CODES, TRUE);
}

QString userMapPath()
{
    gchar *path = g_build_filename(purple_user_dir(), "emoji.txt", NULL);
#ifdef _WIN32
    const QString r = qs(path);
#else
    const QString r = path ? QFile::decodeName(path) : QString();
#endif
    g_free(path);
    return r;
}

bool isReaction(const QString &html)
{
    /* "Reacted with a :rainbowsmile:" / "Reacted to ..." */
    static const QRegularExpression direct(
        QStringLiteral("^\\s*reacted\\s+(?:with|to)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    /* "Andy Smith reacted to your message with :like:" */
    static const QRegularExpression named(
        QStringLiteral("^\\s*(?:\\S+\\s+){1,3}reacted\\s+(?:with|to)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression code(QStringLiteral(":[A-Za-z0-9_+\\-]{1,48}:"));

    gchar *plain = purple_markup_strip_html(html.toUtf8().constData());
    const QString text = qs(plain).trimmed();
    g_free(plain);

    /* Reaction notices are short; ordinary sentences that merely contain
     * "reacted with" are not. */
    if (text.isEmpty() || text.size() > 160)
        return false;
    if (direct.match(text).hasMatch())
        return true;
    if (!named.match(text).hasMatch())
        return false;

    /* With a name in front, also require an emoji or a :code:. */
    if (code.match(text).hasMatch())
        return true;
    for (const QChar c : text) {
        if (c.isHighSurrogate() || c.category() == QChar::Symbol_Other)
            return true;
    }
    return false;
}

QString render(const QString &html, bool codes)
{
    QString out = replaceBlockedImages(html);
    if (codes) {
        ensureBuiltin();
        refreshUserMap();
        out = convertCodes(out);
    }
    return out;
}

} // namespace Emoji
