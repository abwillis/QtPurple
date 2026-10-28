/*
 * QtPurple - a Qt front end for libpurple
 *
 * Tabbed conversation window and PurpleConversationUiOps.
 */
#include "conversationwindow.h"

#include "blistwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cstring>
#include <ctime>

static ConversationWindow *s_window = nullptr;
static int s_signalHandle = 0;

static bool conversationExists(PurpleConversation *conv)
{
    return conv && g_list_find(purple_get_conversations(), conv) != nullptr;
}

static PurplePluginProtocolInfo *prplInfo(PurpleConnection *gc)
{
    if (!gc)
        return nullptr;
    PurplePlugin *prpl = purple_connection_get_prpl(gc);
    return prpl ? PURPLE_PLUGIN_PROTOCOL_INFO(prpl) : nullptr;
}

/* ============================================================ HistoryView */

HistoryView::HistoryView(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setReadOnly(true);
    document()->setMaximumBlockCount(3000);
}

QVariant HistoryView::loadResource(int, const QUrl &)
{
    /* Images we want are registered with QTextDocument::addResource();
     * never fetch anything else (remote or file: URLs from messages). */
    return QVariant();
}

/* ============================================================== InputEdit */

InputEdit::InputEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setTabChangesFocus(false);
}

void InputEdit::addHistory(const QString &text)
{
    if (m_history.isEmpty() || m_history.last() != text)
        m_history.append(text);
    while (m_history.size() > 100)
        m_history.removeFirst();
    m_historyPos = -1;
    m_draft.clear();
}

void InputEdit::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !(mods & Qt::ShiftModifier)) {
        Q_EMIT sendRequested();
        return;
    }
    if (key == Qt::Key_Tab && mods == Qt::NoModifier) {
        if (m_tabCompletion)
            Q_EMIT completeRequested();
        else
            focusNextChild();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Up) {
        if (m_history.isEmpty())
            return;
        if (m_historyPos < 0) {
            m_draft = toPlainText();
            m_historyPos = m_history.size() - 1;
        } else if (m_historyPos > 0) {
            --m_historyPos;
        }
        setPlainText(m_history.at(m_historyPos));
        moveCursor(QTextCursor::End);
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Down) {
        if (m_historyPos < 0)
            return;
        ++m_historyPos;
        if (m_historyPos >= m_history.size()) {
            m_historyPos = -1;
            setPlainText(m_draft);
        } else {
            setPlainText(m_history.at(m_historyPos));
        }
        moveCursor(QTextCursor::End);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

/* ========================================================== chat user item */

namespace {

enum { RoleName = Qt::UserRole, RoleFlags, RoleSortKey };

int flagRank(int flags)
{
    if (flags & PURPLE_CBFLAGS_FOUNDER) return 4;
    if (flags & PURPLE_CBFLAGS_OP)      return 3;
    if (flags & PURPLE_CBFLAGS_HALFOP)  return 2;
    if (flags & PURPLE_CBFLAGS_VOICE)   return 1;
    return 0;
}

class UserItem : public QListWidgetItem
{
public:
    bool operator<(const QListWidgetItem &other) const override
    {
        const int a = flagRank(data(RoleFlags).toInt());
        const int b = flagRank(other.data(RoleFlags).toInt());
        if (a != b)
            return a > b;
        return QString::compare(data(RoleSortKey).toString(),
                                other.data(RoleSortKey).toString(), Qt::CaseInsensitive) < 0;
    }
};

} // namespace

/* ======================================================= ConversationView */

static void dimLabel(QLabel *label)
{
    QPalette p = label->palette();
    QColor c = p.color(QPalette::WindowText);
    c.setAlpha(150);
    p.setColor(QPalette::WindowText, c);
    label->setPalette(p);
}

ConversationView::ConversationView(PurpleConversation *conv, QWidget *parent)
    : QWidget(parent), m_conv(conv)
{
    /* Header */
    m_icon = new QLabel(this);
    m_icon->setFixedSize(48, 48);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->hide();

    m_title = new QLabel(this);
    m_title->setTextFormat(Qt::PlainText);
    QFont tf = m_title->font();
    tf.setBold(true);
    if (tf.pointSizeF() > 0)
        tf.setPointSizeF(tf.pointSizeF() * 1.15);
    m_title->setFont(tf);

    m_subtitle = new QLabel(this);
    m_subtitle->setTextFormat(Qt::PlainText);
    m_subtitle->setWordWrap(true);
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dimLabel(m_subtitle);

    QVBoxLayout *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    QHBoxLayout *header = new QHBoxLayout;
    header->addWidget(m_icon);
    header->addLayout(titles, 1);

    /* History */
    m_view = new HistoryView(this);
    connect(m_view, &QTextBrowser::anchorClicked, this, &ConversationView::onAnchorClicked);

    /* Input */
    m_typing = new QLabel(this);
    QFont small = m_typing->font();
    small.setItalic(true);
    m_typing->setFont(small);
    dimLabel(m_typing);

    m_input = new InputEdit(this);
    m_input->setTabCompletion(isChat());
    m_input->setMinimumHeight(fontMetrics().lineSpacing() * 2 + 12);
    connect(m_input, &InputEdit::sendRequested, this, &ConversationView::send);
    connect(m_input, &InputEdit::completeRequested, this, &ConversationView::completeNick);
    connect(m_input, &QPlainTextEdit::textChanged, this, &ConversationView::onTextChanged);

    QSplitter *vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->setChildrenCollapsible(false);

    if (isChat()) {
        QSplitter *hsplit = new QSplitter(Qt::Horizontal, vsplit);
        hsplit->addWidget(m_view);

        QWidget *panel = new QWidget(hsplit);
        QVBoxLayout *pl = new QVBoxLayout(panel);
        pl->setContentsMargins(0, 0, 0, 0);
        m_userCount = new QLabel(panel);
        m_users = new QListWidget(panel);
        m_users->setContextMenuPolicy(Qt::CustomContextMenu);
        m_users->setSortingEnabled(false);      /* sorted explicitly after batches */
        connect(m_users, &QListWidget::itemActivated, this, &ConversationView::onUserActivated);
        connect(m_users, &QListWidget::customContextMenuRequested, this, &ConversationView::onUserContextMenu);
        pl->addWidget(m_userCount);
        pl->addWidget(m_users, 1);
        hsplit->addWidget(panel);
        hsplit->setStretchFactor(0, 1);
        hsplit->setSizes({ 480, 160 });
        vsplit->addWidget(hsplit);
        updateUserCount();
    } else {
        vsplit->addWidget(m_view);
    }

    QWidget *bottom = new QWidget(vsplit);
    QVBoxLayout *bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(2);
    bl->addWidget(m_typing);
    bl->addWidget(m_input, 1);
    vsplit->addWidget(bottom);
    vsplit->setStretchFactor(0, 1);
    vsplit->setSizes({ 320, 90 });

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addLayout(header);
    lay->addWidget(vsplit, 1);

    setFocusProxy(m_input);
    updateHeader();
}

bool ConversationView::isChat() const
{
    return m_conv && purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT;
}

void ConversationView::detach()
{
    m_conv = nullptr;
    m_input->setEnabled(false);
}

QString ConversationView::title() const
{
    return m_conv ? qs(purple_conversation_get_title(m_conv)) : m_title->text();
}

QIcon ConversationView::icon() const
{
    if (!m_conv)
        return QIcon();
    if (isChat())
        return QtPurple::chatIcon();
    PurpleBuddy *buddy = purple_find_buddy(purple_conversation_get_account(m_conv),
                                           purple_conversation_get_name(m_conv));
    if (!buddy || !PURPLE_BUDDY_IS_ONLINE(buddy))
        return QtPurple::statusIcon(PURPLE_STATUS_OFFLINE);
    PurplePresence *presence = purple_buddy_get_presence(buddy);
    PurpleStatus *status = purple_presence_get_active_status(presence);
    if (!status)
        return QtPurple::statusIcon(PURPLE_STATUS_OFFLINE);
    return QtPurple::statusIcon(purple_status_type_get_primitive(purple_status_get_type(status)),
                                purple_presence_is_idle(presence));
}

void ConversationView::setActivity(Activity a)
{
    if (a == m_activity)
        return;
    m_activity = a;
    Q_EMIT activityChanged(this);
}

void ConversationView::focusInput()
{
    m_input->setFocus();
}

void ConversationView::clearHistory()
{
    m_view->clear();
}

/* ------------------------------------------------------------- messages */

QString ConversationView::processImages(const QString &html)
{
    /* libpurple embeds stored images as <img id="N">. */
    static const QRegularExpression re(
        QStringLiteral("<img\\b[^>]*?\\bid\\s*=\\s*[\"']?(\\d+)[\"']?[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();

        const int id = m.captured(1).toInt();
        PurpleStoredImage *img = purple_imgstore_find_by_id(id);
        QImage image;
        if (img) {
            image.loadFromData(static_cast<const uchar *>(purple_imgstore_get_data(img)),
                               int(purple_imgstore_get_size(img)));
        }
        if (image.isNull()) {
            out += tr("[image]");
            continue;
        }
        const QUrl url(QStringLiteral("purpleimg:%1-%2").arg(id).arg(++m_imageCounter));
        m_view->document()->addResource(QTextDocument::ImageResource, url, image);
        const int maxWidth = 360;
        if (image.width() > maxWidth)
            out += QStringLiteral("<img src=\"%1\" width=\"%2\">").arg(url.toString()).arg(maxWidth);
        else
            out += QStringLiteral("<img src=\"%1\">").arg(url.toString());
    }
    out += html.mid(last);
    return out;
}

void ConversationView::writeMessage(const char *who, const char *alias, const char *message,
                                    PurpleMessageFlags flags, time_t mtime)
{
    if (!m_conv)
        return;

    const QDateTime when = QDateTime::fromSecsSinceEpoch(qint64(mtime ? mtime : time(nullptr)));
    const bool showDate = (flags & PURPLE_MESSAGE_DELAYED) || when.date() != QDate::currentDate();
    const QString ts = when.toString(showDate ? QStringLiteral("yyyy-MM-dd HH:mm:ss")
                                              : QStringLiteral("HH:mm:ss"));

    /* "/me" handling, then linkify like Pidgin does for display. */
    gchar *copy = g_strdup(message ? message : "");
    const bool isMe = purple_message_meify(copy, -1);
    gchar *linked = purple_markup_linkify(copy);
    const QString body = processImages(qs(linked));
    g_free(linked);
    g_free(copy);

    const QString name = qs(alias && *alias ? alias : who).toHtmlEscaped();
    const QString stamp = QStringLiteral("<span style=\"color:#888a85\">(%1)</span> ").arg(ts);

    QString line;
    if ((flags & (PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ERROR | PURPLE_MESSAGE_NOTIFY)) || name.isEmpty()) {
        const QString color = (flags & PURPLE_MESSAGE_ERROR) ? QStringLiteral("#cc0000")
                                                             : QStringLiteral("#555753");
        line = stamp + QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, body);
    } else {
        QString color = (flags & PURPLE_MESSAGE_SEND) ? QStringLiteral("#204a87")
                                                      : QStringLiteral("#a40000");
        QString shown = name;
        if (flags & PURPLE_MESSAGE_AUTO_RESP)
            shown += tr(" &lt;AUTO-REPLY&gt;");
        if (isMe)
            line = stamp + QStringLiteral("<b style=\"color:%1\">***%2</b> %3").arg(color, shown, body);
        else
            line = stamp + QStringLiteral("<b style=\"color:%1\">%2:</b> %3").arg(color, shown, body);
        if (flags & PURPLE_MESSAGE_NICK)
            line = QStringLiteral("<span style=\"background-color:#fce94f\">%1</span>").arg(line);
    }

    QScrollBar *sb = m_view->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    m_view->append(line);
    if (atBottom || (flags & PURPLE_MESSAGE_SEND))
        sb->setValue(sb->maximum());

    /* Unread indicators. */
    ConversationWindow *win = ConversationWindow::instance();
    if (!win->isViewVisibleToUser(this)) {
        Activity a = NoActivity;
        if (flags & PURPLE_MESSAGE_RECV)
            a = ((flags & PURPLE_MESSAGE_NICK) || !isChat()) ? NickSaid : NewMessage;
        else if (flags & (PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ERROR))
            a = NewEvent;
        if (a > m_activity)
            setActivity(a);
        if ((flags & PURPLE_MESSAGE_RECV) && (a == NickSaid))
            QApplication::alert(win);
    }
}

void ConversationView::systemMessage(const QString &text, bool error)
{
    if (!m_conv)
        return;
    const PurpleMessageFlags flags = static_cast<PurpleMessageFlags>(
        (error ? PURPLE_MESSAGE_ERROR : PURPLE_MESSAGE_SYSTEM) | PURPLE_MESSAGE_NO_LOG);
    purple_conversation_write(m_conv, "", Utf8(text.toHtmlEscaped()), flags, time(nullptr));
}

void ConversationView::send()
{
    if (!m_conv)
        return;

    QString text = m_input->toPlainText();
    if (text.trimmed().isEmpty())
        return;

    m_input->addHistory(text);
    {
        /* Don't let clearing the box send a "stopped typing" notification. */
        const QSignalBlocker block(m_input);
        m_input->clear();
    }

    PurpleConnection *gc = purple_conversation_get_gc(m_conv);
    PurplePluginProtocolInfo *info = prplInfo(gc);

    if (text.startsWith(QLatin1Char('/')) && !text.startsWith(QLatin1String("//"))) {
        const QString cmdline = text.mid(1);
        QString markup = cmdline.toHtmlEscaped();
        markup.replace(QLatin1Char('\n'), QLatin1String("<br>"));
        gchar *error = nullptr;
        const PurpleCmdStatus status = purple_cmd_do_command(m_conv, Utf8(cmdline), Utf8(markup), &error);
        const QString err = qs(error);
        g_free(error);

        bool handled = true;
        switch (status) {
        case PURPLE_CMD_STATUS_OK:
            break;
        case PURPLE_CMD_STATUS_NOT_FOUND:
            if (info && (info->options & OPT_PROTO_SLASH_COMMANDS_NATIVE))
                handled = false;    /* the protocol understands it natively */
            else
                systemMessage(tr("No such command."), true);
            break;
        case PURPLE_CMD_STATUS_WRONG_ARGS:
            systemMessage(tr("Syntax error: wrong number of arguments for that command."), true);
            break;
        case PURPLE_CMD_STATUS_FAILED:
            systemMessage(err.isEmpty() ? tr("The command failed for an unknown reason.") : err, true);
            break;
        case PURPLE_CMD_STATUS_WRONG_TYPE:
            systemMessage(isChat() ? tr("That command only works in IMs, not chats.")
                                   : tr("That command only works in chats, not IMs."), true);
            break;
        case PURPLE_CMD_STATUS_WRONG_PRPL:
            systemMessage(tr("That command doesn't work on this protocol."), true);
            break;
        }
        if (handled)
            return;
        /* m_conv may have been destroyed by a command such as /part. */
        if (!m_conv)
            return;
    } else if (text.startsWith(QLatin1String("//"))) {
        text.remove(0, 1);
    }

    if (!gc || !QtPurple::connectionExists(gc)) {
        systemMessage(tr("Message not sent: you are not connected."), true);
        return;
    }

    QString html = text.toHtmlEscaped();
    html.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    Utf8 msg(html);

    purple_idle_touch();
    if (isChat()) {
        purple_conv_chat_send(PURPLE_CONV_CHAT(m_conv), msg);
    } else {
        PurpleConvIm *im = PURPLE_CONV_IM(m_conv);
        purple_conv_im_stop_send_typed_timeout(im);
        purple_conv_im_set_type_again(im, 0);
        purple_conv_im_send(im, msg);
    }
}

void ConversationView::onTextChanged()
{
    if (!m_conv || isChat())
        return;
    if (!purple_prefs_get_bool("/purple/conversations/im/send_typing"))
        return;
    PurpleConnection *gc = purple_conversation_get_gc(m_conv);
    if (!gc)
        return;

    PurpleConvIm *im = PURPLE_CONV_IM(m_conv);
    const char *name = purple_conversation_get_name(m_conv);

    if (m_input->document()->isEmpty()) {
        purple_conv_im_stop_send_typed_timeout(im);
        purple_conv_im_set_type_again(im, 0);
        serv_send_typing(gc, name, PURPLE_NOT_TYPING);
        return;
    }

    if (purple_conv_im_get_send_typed_timeout(im))
        purple_conv_im_stop_send_typed_timeout(im);
    purple_conv_im_start_send_typed_timeout(im);

    if (time(nullptr) > purple_conv_im_get_type_again(im)) {
        const unsigned int timeout = serv_send_typing(gc, name, PURPLE_TYPING);
        purple_conv_im_set_type_again(im, timeout);
    }
}

void ConversationView::onAnchorClicked(const QUrl &url)
{
    QtPurple::openLink(url);
}

/* -------------------------------------------------------------- header */

void ConversationView::updateHeader()
{
    if (!m_conv)
        return;

    m_title->setText(title());
    PurpleAccount *account = purple_conversation_get_account(m_conv);
    QString sub;

    if (isChat()) {
        PurpleConvChat *chat = PURPLE_CONV_CHAT(m_conv);
        const bool left = purple_conv_chat_has_left(chat);
        QString topic;
        if (const char *t = purple_conv_chat_get_topic(chat)) {
            gchar *plain = purple_markup_strip_html(t);
            topic = qs(plain).simplified();
            g_free(plain);
        }
        if (left)
            sub = tr("You are not in this chat.");
        else if (!topic.isEmpty())
            sub = topic;
        else
            sub = QtPurple::accountDisplayName(account);
        if (m_users)
            m_users->setEnabled(!left);
        m_icon->hide();
    } else {
        const char *name = purple_conversation_get_name(m_conv);
        PurpleBuddy *buddy = purple_find_buddy(account, name);
        if (buddy && PURPLE_BUDDY_IS_ONLINE(buddy)) {
            PurpleStatus *status = purple_presence_get_active_status(purple_buddy_get_presence(buddy));
            if (status) {
                sub = qs(purple_status_get_name(status));
                if (const char *msg = purple_status_get_attr_string(status, "message")) {
                    gchar *plain = purple_markup_strip_html(msg);
                    const QString m = qs(plain).simplified();
                    g_free(plain);
                    if (!m.isEmpty())
                        sub += QStringLiteral(": ") + m;
                }
            }
        } else if (buddy) {
            sub = tr("Offline");
        }
        const QString via = tr("via %1").arg(QtPurple::accountDisplayName(account));
        sub = sub.isEmpty() ? via : sub + QStringLiteral("  \u2014  ") + via;

        PurpleBuddyIcon *icon = purple_conv_im_get_icon(PURPLE_CONV_IM(m_conv));
        bool unref = false;
        if (!icon) {
            icon = purple_buddy_icons_find(account, name);
            unref = (icon != nullptr);
        }
        QPixmap pm;
        if (icon) {
            size_t len = 0;
            gconstpointer data = purple_buddy_icon_get_data(icon, &len);
            if (data && len > 0)
                pm.loadFromData(static_cast<const uchar *>(data), uint(len));
            if (unref)
                purple_buddy_icon_unref(icon);
        }
        if (!pm.isNull()) {
            m_icon->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_icon->show();
        } else {
            m_icon->hide();
        }
    }

    m_subtitle->setText(sub);
    Q_EMIT titleChanged(this);
}

void ConversationView::conversationUpdated(PurpleConvUpdateType type)
{
    if (!m_conv)
        return;

    switch (type) {
    case PURPLE_CONV_UPDATE_TYPING:
        if (!isChat()) {
            const QString who = title();
            switch (purple_conv_im_get_typing_state(PURPLE_CONV_IM(m_conv))) {
            case PURPLE_TYPING:
                m_typing->setText(tr("%1 is typing...").arg(who));
                if (m_activity <= Typing && !ConversationWindow::instance()->isViewVisibleToUser(this))
                    setActivity(Typing);
                break;
            case PURPLE_TYPED:
                m_typing->setText(tr("%1 has stopped typing").arg(who));
                if (m_activity == Typing)
                    setActivity(NoActivity);
                break;
            default:
                m_typing->clear();
                if (m_activity == Typing)
                    setActivity(NoActivity);
                break;
            }
        }
        break;
    case PURPLE_CONV_UPDATE_ADD:
    case PURPLE_CONV_UPDATE_REMOVE:
    case PURPLE_CONV_UPDATE_ACCOUNT:
    case PURPLE_CONV_UPDATE_TOPIC:
    case PURPLE_CONV_UPDATE_AWAY:
    case PURPLE_CONV_UPDATE_ICON:
    case PURPLE_CONV_UPDATE_TITLE:
    case PURPLE_CONV_UPDATE_CHATLEFT:
        updateHeader();
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ chat users */

void ConversationView::setUserItem(QListWidgetItem *item, const QString &name,
                                   const QString &alias, int flags)
{
    QString prefix;
    if (flags & PURPLE_CBFLAGS_FOUNDER)
        prefix = QStringLiteral("~");
    else if (flags & PURPLE_CBFLAGS_OP)
        prefix = QStringLiteral("@");
    else if (flags & PURPLE_CBFLAGS_HALFOP)
        prefix = QStringLiteral("%");
    else if (flags & PURPLE_CBFLAGS_VOICE)
        prefix = QStringLiteral("+");

    const QString shown = alias.isEmpty() ? name : alias;
    item->setText(prefix + shown);
    item->setData(RoleName, name);
    item->setData(RoleFlags, flags);
    item->setData(RoleSortKey, shown);
    item->setToolTip(shown == name ? name : QStringLiteral("%1 (%2)").arg(shown, name));

    QFont f = m_users->font();
    f.setItalic((flags & PURPLE_CBFLAGS_TYPING) != 0);
    item->setFont(f);
    item->setForeground((flags & PURPLE_CBFLAGS_AWAY)
                            ? m_users->palette().brush(QPalette::Disabled, QPalette::Text)
                            : m_users->palette().brush(QPalette::Active, QPalette::Text));
}

void ConversationView::updateUserCount()
{
    if (m_userCount)
        m_userCount->setText(tr("%n user(s) in room", nullptr, m_users ? m_users->count() : 0));
}

void ConversationView::addUsers(GList *cbuddies)
{
    if (!m_users)
        return;
    for (GList *l = cbuddies; l; l = l->next) {
        PurpleConvChatBuddy *cb = static_cast<PurpleConvChatBuddy *>(l->data);
        const QString name = qs(cb->name);
        QListWidgetItem *item = m_userItems.value(name, nullptr);
        if (!item) {
            item = new UserItem();
            m_users->addItem(item);
            m_userItems.insert(name, item);
        }
        setUserItem(item, name, qs(cb->alias), int(cb->flags));
    }
    m_users->sortItems();
    updateUserCount();
}

void ConversationView::renameUser(const char *oldName, const char *newName, const char *newAlias)
{
    if (!m_users || !m_conv)
        return;
    const QString oldN = qs(oldName);
    const QString newN = qs(newName);

    QListWidgetItem *item = m_userItems.take(oldN);
    int flags = item ? item->data(RoleFlags).toInt() : 0;
    if (PurpleConvChatBuddy *cb = purple_conv_chat_cb_find(PURPLE_CONV_CHAT(m_conv), newName))
        flags = int(cb->flags);

    if (QListWidgetItem *dup = m_userItems.take(newN)) {
        if (dup != item)
            delete dup;
    }
    if (!item) {
        item = new UserItem();
        m_users->addItem(item);
    }
    m_userItems.insert(newN, item);
    setUserItem(item, newN, qs(newAlias), flags);
    m_users->sortItems();
    updateUserCount();
}

void ConversationView::removeUsers(GList *names)
{
    if (!m_users)
        return;
    for (GList *l = names; l; l = l->next) {
        const QString name = qs(static_cast<const char *>(l->data));
        delete m_userItems.take(name);
    }
    updateUserCount();
}

void ConversationView::updateUser(const char *name)
{
    if (!m_users || !m_conv)
        return;
    PurpleConvChatBuddy *cb = purple_conv_chat_cb_find(PURPLE_CONV_CHAT(m_conv), name);
    if (!cb)
        return;
    const QString n = qs(name);
    QListWidgetItem *item = m_userItems.value(n, nullptr);
    if (!item) {
        item = new UserItem();
        m_users->addItem(item);
        m_userItems.insert(n, item);
    }
    setUserItem(item, n, qs(cb->alias), int(cb->flags));
    m_users->sortItems();
    updateUserCount();
}

QString ConversationView::realName(const QString &chatName) const
{
    if (!m_conv)
        return chatName;
    PurpleConnection *gc = purple_conversation_get_gc(m_conv);
    PurplePluginProtocolInfo *info = prplInfo(gc);
    if (info && info->get_cb_real_name) {
        char *real = info->get_cb_real_name(gc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(m_conv)),
                                            Utf8(chatName));
        if (real) {
            const QString r = qs(real);
            g_free(real);
            return r;
        }
    }
    return chatName;
}

void ConversationView::onUserActivated(QListWidgetItem *item)
{
    if (!m_conv || !item)
        return;
    PurpleAccount *account = purple_conversation_get_account(m_conv);
    if (!purple_account_is_connected(account))
        return;
    Utf8 real(realName(item->data(RoleName).toString()));
    PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, real, account);
    if (!conv)
        conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, real);
    if (conv)
        purple_conversation_present(conv);
}

void ConversationView::onUserContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_users->itemAt(pos);
    if (!item || !m_conv)
        return;
    const QString name = item->data(RoleName).toString();
    PurpleConversation *conv = m_conv;

    /* exec() below runs a nested event loop during which the user list may
     * change, so actions look the item up again by name. */
    QMenu menu(this);
    menu.addAction(tr("&IM"), this, [this, name]() {
        if (QListWidgetItem *it = m_userItems.value(name, nullptr))
            onUserActivated(it);
    });
    menu.addAction(tr("Get &Info"), this, [this, conv, name]() {
        if (!conversationExists(conv) || m_conv != conv)
            return;
        PurpleConnection *gc = purple_conversation_get_gc(conv);
        PurplePluginProtocolInfo *info = prplInfo(gc);
        if (!gc)
            return;
        if (info && info->get_cb_info)
            info->get_cb_info(gc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv)), Utf8(name));
        else
            serv_get_info(gc, Utf8(realName(name)));
    });

    const bool ignored = purple_conv_chat_is_user_ignored(PURPLE_CONV_CHAT(conv), Utf8(name));
    menu.addAction(ignored ? tr("Un-&ignore") : tr("&Ignore"), this, [this, conv, name, ignored]() {
        if (!conversationExists(conv) || m_conv != conv)
            return;
        if (ignored)
            purple_conv_chat_unignore(PURPLE_CONV_CHAT(conv), Utf8(name));
        else
            purple_conv_chat_ignore(PURPLE_CONV_CHAT(conv), Utf8(name));
        updateUser(Utf8(name));
    });
    menu.addSeparator();
    menu.addAction(tr("&Mention"), this, [this, name]() {
        m_input->insertPlainText(name + QStringLiteral(": "));
        m_input->setFocus();
    });

    menu.exec(m_users->viewport()->mapToGlobal(pos));
}

void ConversationView::completeNick()
{
    if (!m_users)
        return;
    QTextCursor cur = m_input->textCursor();
    const QString before = m_input->toPlainText().left(cur.position());
    int start = before.size();
    while (start > 0 && !before.at(start - 1).isSpace())
        --start;
    const QString word = before.mid(start);
    if (word.isEmpty())
        return;

    QStringList matches;
    for (auto it = m_userItems.constBegin(); it != m_userItems.constEnd(); ++it) {
        if (it.key().startsWith(word, Qt::CaseInsensitive))
            matches.append(it.key());
    }
    if (matches.isEmpty())
        return;
    matches.sort(Qt::CaseInsensitive);

    QString completion;
    if (matches.size() == 1) {
        completion = matches.first() + (start == 0 ? QStringLiteral(": ") : QStringLiteral(" "));
    } else {
        /* Longest common prefix, then list the candidates. */
        completion = matches.first();
        for (const QString &m : matches) {
            int i = 0;
            while (i < completion.size() && i < m.size() &&
                   completion.at(i).toLower() == m.at(i).toLower())
                ++i;
            completion.truncate(i);
        }
        if (completion.size() < word.size())
            completion = word;
        const QString list = matches.join(QStringLiteral(" "));
        m_view->append(QStringLiteral("<span style=\"color:#555753\">%1</span>").arg(list.toHtmlEscaped()));
    }

    cur.setPosition(start);
    cur.setPosition(before.size(), QTextCursor::KeepAnchor);
    cur.insertText(completion);
    m_input->setTextCursor(cur);
}

/* ================================================================ ui ops */

ConversationView *ConversationWindow::viewFor(PurpleConversation *conv)
{
    return conv ? static_cast<ConversationView *>(conv->ui_data) : nullptr;
}

static void convCreate(PurpleConversation *conv)
{
    ConversationView *view = new ConversationView(conv);
    conv->ui_data = view;
    ConversationWindow::instance()->addView(view);
}

static void convDestroy(PurpleConversation *conv)
{
    ConversationView *view = ConversationWindow::viewFor(conv);
    conv->ui_data = nullptr;
    if (!view)
        return;
    view->detach();
    ConversationWindow::instance()->removeView(view);
}

static void convWrite(PurpleConversation *conv, const char *name, const char *alias,
                      const char *message, PurpleMessageFlags flags, time_t mtime)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->writeMessage(name, alias, message, flags, mtime);
}

static void convChatAddUsers(PurpleConversation *conv, GList *cbuddies, gboolean)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->addUsers(cbuddies);
}

static void convChatRenameUser(PurpleConversation *conv, const char *oldName,
                               const char *newName, const char *newAlias)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->renameUser(oldName, newName, newAlias);
}

static void convChatRemoveUsers(PurpleConversation *conv, GList *users)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->removeUsers(users);
}

static void convChatUpdateUser(PurpleConversation *conv, const char *user)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->updateUser(user);
}

static void convPresent(PurpleConversation *conv)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        ConversationWindow::instance()->presentView(view);
}

static gboolean convHasFocus(PurpleConversation *conv)
{
    ConversationView *view = ConversationWindow::viewFor(conv);
    return (view && ConversationWindow::instance()->viewHasFocus(view)) ? TRUE : FALSE;
}

PurpleConversationUiOps *ConversationWindow::uiOps()
{
    static PurpleConversationUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.create_conversation = convCreate;
        ops.destroy_conversation = convDestroy;
        ops.write_conv = convWrite;
        ops.chat_add_users = convChatAddUsers;
        ops.chat_rename_user = convChatRenameUser;
        ops.chat_remove_users = convChatRemoveUsers;
        ops.chat_update_user = convChatUpdateUser;
        ops.present = convPresent;
        ops.has_focus = convHasFocus;
        ready = true;
    }
    return &ops;
}

static void onConversationUpdated(PurpleConversation *conv, PurpleConvUpdateType type, gpointer)
{
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->conversationUpdated(type);
}

static void onBuddyChanged(PurpleBuddy *buddy, gpointer)
{
    PurpleConversation *conv = purple_find_conversation_with_account(
        PURPLE_CONV_TYPE_IM, purple_buddy_get_name(buddy), purple_buddy_get_account(buddy));
    if (ConversationView *view = ConversationWindow::viewFor(conv))
        view->updateHeader();
}

static void onBuddyStatusChanged(PurpleBuddy *buddy, PurpleStatus *, PurpleStatus *, gpointer data)
{
    onBuddyChanged(buddy, data);
}

void ConversationWindow::init()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    purple_signal_connect(purple_conversations_get_handle(), "conversation-updated",
                          &s_signalHandle, PURPLE_CALLBACK(onConversationUpdated), nullptr);
    void *blist = purple_blist_get_handle();
    purple_signal_connect(blist, "buddy-status-changed", &s_signalHandle,
                          PURPLE_CALLBACK(onBuddyStatusChanged), nullptr);
    purple_signal_connect(blist, "buddy-signed-on", &s_signalHandle,
                          PURPLE_CALLBACK(onBuddyChanged), nullptr);
    purple_signal_connect(blist, "buddy-signed-off", &s_signalHandle,
                          PURPLE_CALLBACK(onBuddyChanged), nullptr);
    purple_signal_connect(blist, "buddy-icon-changed", &s_signalHandle,
                          PURPLE_CALLBACK(onBuddyChanged), nullptr);
}

/* ===================================================== ConversationWindow */

ConversationWindow *ConversationWindow::instance()
{
    if (!s_window)
        s_window = new ConversationWindow();
    return s_window;
}

ConversationWindow::ConversationWindow()
    : QMainWindow(nullptr)
{
    setWindowTitle(tr("Conversations"));

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setElideMode(Qt::ElideRight);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &ConversationWindow::onTabCloseRequested);
    connect(m_tabs, &QTabWidget::currentChanged, this, &ConversationWindow::onCurrentChanged);
    setCentralWidget(m_tabs);

    m_convMenu = menuBar()->addMenu(tr("&Conversation"));
    connect(m_convMenu, &QMenu::aboutToShow, this, &ConversationWindow::populateConversationMenu);

    /* Permanent shortcuts (the menu itself is rebuilt on demand). */
    QAction *close = new QAction(this);
    close->setShortcut(QKeySequence::Close);
    connect(close, &QAction::triggered, this, [this]() { onTabCloseRequested(m_tabs->currentIndex()); });
    addAction(close);

    QAction *next = new QAction(this);
    next->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown));
    connect(next, &QAction::triggered, this, [this]() {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    addAction(next);

    QAction *prev = new QAction(this);
    prev->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp));
    connect(prev, &QAction::triggered, this, [this]() {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    addAction(prev);

    const int w = purple_prefs_get_int(QTPURPLE_PREF_CONV_W);
    const int h = purple_prefs_get_int(QTPURPLE_PREF_CONV_H);
    resize(w > 200 ? w : 680, h > 150 ? h : 480);
}

ConversationView *ConversationWindow::currentView() const
{
    return qobject_cast<ConversationView *>(m_tabs->currentWidget());
}

static QString tabLabel(const QString &title)
{
    QString t = title;
    t.replace(QLatin1Char('&'), QLatin1String("&&"));
    return t;
}

void ConversationWindow::addView(ConversationView *view)
{
    const int idx = m_tabs->addTab(view, view->icon(), tabLabel(view->title()));
    m_tabs->setTabToolTip(idx, view->title());
    connect(view, &ConversationView::titleChanged, this, &ConversationWindow::onTitleChanged);
    connect(view, &ConversationView::activityChanged, this, &ConversationWindow::onActivityChanged);

    if (!isVisible()) {
        /* Show new incoming conversations without stealing focus. */
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        show();
        setAttribute(Qt::WA_ShowWithoutActivating, false);
    }
    if (m_tabs->count() == 1)
        onCurrentChanged(0);
}

void ConversationWindow::removeView(ConversationView *view)
{
    const int idx = m_tabs->indexOf(view);
    if (idx >= 0)
        m_tabs->removeTab(idx);
    view->hide();
    view->deleteLater();
    if (m_tabs->count() == 0) {
        saveGeometryPrefs();
        hide();
    }
}

void ConversationWindow::presentView(ConversationView *view)
{
    if (m_tabs->indexOf(view) < 0)
        return;
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
    m_tabs->setCurrentWidget(view);
    view->setActivity(ConversationView::NoActivity);
    view->focusInput();
}

bool ConversationWindow::viewHasFocus(ConversationView *view) const
{
    return isActiveWindow() && currentView() == view;
}

bool ConversationWindow::isViewVisibleToUser(ConversationView *view) const
{
    return isVisible() && !isMinimized() && isActiveWindow() && currentView() == view;
}

void ConversationWindow::saveGeometryPrefs()
{
    if (!isVisible())
        return;
    purple_prefs_set_int(QTPURPLE_PREF_CONV_W, width());
    purple_prefs_set_int(QTPURPLE_PREF_CONV_H, height());
}

void ConversationWindow::onTabCloseRequested(int index)
{
    ConversationView *view = qobject_cast<ConversationView *>(m_tabs->widget(index));
    if (!view)
        return;
    PurpleConversation *conv = view->conversation();
    if (conv && conversationExists(conv))
        purple_conversation_destroy(conv);     /* calls convDestroy -> removeView */
    else
        removeView(view);
}

void ConversationWindow::onCurrentChanged(int)
{
    ConversationView *view = currentView();
    if (!view) {
        setWindowTitle(tr("Conversations"));
        return;
    }
    setWindowTitle(view->title());
    if (isActiveWindow())
        view->setActivity(ConversationView::NoActivity);
    view->focusInput();
}

void ConversationWindow::onTitleChanged(ConversationView *view)
{
    const int idx = m_tabs->indexOf(view);
    if (idx < 0)
        return;
    m_tabs->setTabText(idx, tabLabel(view->title()));
    m_tabs->setTabToolTip(idx, view->title());
    m_tabs->setTabIcon(idx, view->icon());
    if (view == currentView())
        setWindowTitle(view->title());
}

void ConversationWindow::onActivityChanged(ConversationView *view)
{
    const int idx = m_tabs->indexOf(view);
    if (idx < 0)
        return;
    QColor color;
    switch (view->activity()) {
    case ConversationView::Typing:     color = QColor(0x4e, 0x9a, 0x06); break;
    case ConversationView::NewEvent:   color = QColor(0x75, 0x50, 0x7b); break;
    case ConversationView::NewMessage: color = QColor(0xcc, 0x00, 0x00); break;
    case ConversationView::NickSaid:   color = QColor(0x34, 0x65, 0xa4); break;
    default: break;
    }
    m_tabs->tabBar()->setTabTextColor(idx, color);
}

void ConversationWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        if (ConversationView *view = currentView()) {
            if (view->activity() != ConversationView::Typing)
                view->setActivity(ConversationView::NoActivity);
        }
    }
    QMainWindow::changeEvent(event);
}

void ConversationWindow::closeEvent(QCloseEvent *event)
{
    saveGeometryPrefs();

    QList<PurpleConversation *> convs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        ConversationView *view = qobject_cast<ConversationView *>(m_tabs->widget(i));
        if (view && view->conversation())
            convs.append(view->conversation());
    }
    for (PurpleConversation *conv : convs) {
        if (conversationExists(conv))
            purple_conversation_destroy(conv);
    }
    /* Views without a live conversation. */
    while (m_tabs->count() > 0) {
        if (ConversationView *view = qobject_cast<ConversationView *>(m_tabs->widget(0)))
            removeView(view);
        else
            m_tabs->removeTab(0);
    }
    event->accept();
}

void ConversationWindow::populateConversationMenu()
{
    m_convMenu->clear();
    ConversationView *view = currentView();
    PurpleConversation *conv = view ? view->conversation() : nullptr;
    if (!conv) {
        m_convMenu->addAction(tr("&Close"), this, [this]() { onTabCloseRequested(m_tabs->currentIndex()); });
        return;
    }

    PurpleAccount *account = purple_conversation_get_account(conv);
    PurpleConnection *gc = purple_conversation_get_gc(conv);
    PurplePluginProtocolInfo *info = prplInfo(gc);
    const QByteArray name(purple_conversation_get_name(conv));
    const bool chat = purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT;

    if (!chat) {
        QAction *sendFile = m_convMenu->addAction(tr("Send &File..."), this, [conv, name]() {
            if (!conversationExists(conv))
                return;
            if (PurpleConnection *c = purple_conversation_get_gc(conv))
                serv_send_file(c, name.constData(), nullptr);
        });
        sendFile->setEnabled(info && info->send_file &&
                             (!info->can_receive_file || info->can_receive_file(gc, name.constData())));

        QAction *getInfo = m_convMenu->addAction(tr("Get &Info"), this, [conv, name]() {
            if (!conversationExists(conv))
                return;
            if (PurpleConnection *c = purple_conversation_get_gc(conv))
                serv_get_info(c, name.constData());
        });
        getInfo->setEnabled(info && info->get_info);

        QAction *add = m_convMenu->addAction(tr("&Add Buddy..."), this, [conv, name]() {
            if (!conversationExists(conv))
                return;
            BuddyListWindow::instance()->showAddBuddyDialog(purple_conversation_get_account(conv),
                                                           QString::fromUtf8(name), QString(), QString());
        });
        add->setEnabled(gc && !purple_find_buddy(account, name.constData()));
    } else {
        QAction *invite = m_convMenu->addAction(tr("In&vite..."), this, [conv]() {
            if (conversationExists(conv) && purple_conversation_get_gc(conv))
                purple_conv_chat_invite_user(PURPLE_CONV_CHAT(conv), nullptr, nullptr, TRUE);
        });
        invite->setEnabled(gc && info && info->chat_invite);
    }

    m_convMenu->addSeparator();
    m_convMenu->addAction(tr("Clear &Scrollback"), this, [view, conv]() {
        if (!conversationExists(conv))
            return;
        purple_conversation_clear_message_history(conv);
        if (ConversationWindow::viewFor(conv) == view)
            view->clearHistory();
    });
    m_convMenu->addSeparator();
    QAction *close = m_convMenu->addAction(tr("&Close"), this, [this]() {
        onTabCloseRequested(m_tabs->currentIndex());
    });
    close->setShortcut(QKeySequence::Close);
}
