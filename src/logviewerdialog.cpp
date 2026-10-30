/*
 * QtPurple - a Qt front end for libpurple
 *
 * Conversation log viewer.
 */
#include "logviewerdialog.h"
#include "conversationwindow.h"     /* HistoryView */
#include "core.h"
#include "emoji.h"

#include <QDateTime>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {

/* One window per conversation, keyed by what the logs are looked up with. */
QHash<QString, QPointer<LogViewerDialog>> g_dialogs;

enum { RoleLogIndex = Qt::UserRole };   /* int: index into m_logs; unset on month rows */

QString accountKey(PurpleAccount *account)
{
    return qs(purple_account_get_protocol_id(account)) + QLatin1Char(':') +
           qs(purple_account_get_username(account));
}

QString sourcesKey(const QList<LogViewer::Source> &sources)
{
    QStringList parts;
    for (const LogViewer::Source &s : sources)
        parts << QStringLiteral("%1|%2|%3").arg(int(s.type)).arg(accountKey(s.account),
                                                                 QString::fromUtf8(s.name));
    return parts.join(QLatin1Char('\n'));
}

bool loggingEnabled(PurpleLogType type)
{
    if (type == PURPLE_LOG_CHAT)
        return purple_prefs_get_bool("/purple/logging/log_chats");
    if (type == PURPLE_LOG_IM)
        return purple_prefs_get_bool("/purple/logging/log_ims");
    return purple_prefs_get_bool("/purple/logging/log_system");
}

void showSources(const QString &title, const QList<LogViewer::Source> &sources)
{
    if (sources.isEmpty())
        return;
    const QString key = sourcesKey(sources);
    QPointer<LogViewerDialog> &slot = g_dialogs[key];
    if (!slot)
        slot = new LogViewerDialog(title, sources);
    slot->show();
    slot->raise();
    slot->activateWindow();
}

QString sizeText(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    gchar *s = purple_str_size_to_units(size_t(bytes));
    const QString r = qs(s);
    g_free(s);
    return r;
}

} // namespace

/* ============================================================== LogViewer */

namespace LogViewer {

void showForConversation(PurpleConversation *conv)
{
    if (!conv)
        return;
    PurpleAccount *account = purple_conversation_get_account(conv);
    const char *name = purple_conversation_get_name(conv);
    if (!account || !name)
        return;

    if (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT) {
        showSources(qs(purple_conversation_get_title(conv)),
                    { Source{ PURPLE_LOG_CHAT, QByteArray(name), account } });
        return;
    }

    /* A buddy: show the logs of the whole contact, like Pidgin. */
    if (PurpleBuddy *buddy = purple_find_buddy(account, name)) {
        showForBuddy(buddy);
        return;
    }
    showSources(qs(purple_conversation_get_title(conv)),
                { Source{ PURPLE_LOG_IM, QByteArray(name), account } });
}

void showForBuddy(PurpleBuddy *buddy)
{
    if (!buddy)
        return;
    QList<Source> sources;
    QString title = qs(purple_buddy_get_alias(buddy));

    if (PurpleContact *contact = purple_buddy_get_contact(buddy)) {
        for (PurpleBlistNode *n = purple_blist_node_get_first_child(PURPLE_BLIST_NODE(contact)); n;
             n = purple_blist_node_get_sibling_next(n)) {
            if (!PURPLE_BLIST_NODE_IS_BUDDY(n))
                continue;
            PurpleBuddy *b = PURPLE_BUDDY(n);
            const char *name = purple_buddy_get_name(b);
            if (name)
                sources.append(Source{ PURPLE_LOG_IM, QByteArray(name), purple_buddy_get_account(b) });
        }
        if (sources.size() > 1)
            title = qs(purple_contact_get_alias(contact));
    }
    if (sources.isEmpty()) {
        const char *name = purple_buddy_get_name(buddy);
        if (!name)
            return;
        sources.append(Source{ PURPLE_LOG_IM, QByteArray(name), purple_buddy_get_account(buddy) });
    }
    showSources(title, sources);
}

void showForChat(PurpleChat *chat)
{
    if (!chat)
        return;
    PurpleAccount *account = purple_chat_get_account(chat);
    if (!account)
        return;

    /* A chat's log is named after the conversation, which the protocol
     * derives from the chat components (e.g. "#channel" on IRC). The
     * protocol plugin is found without a connection so that logs stay
     * reachable while the account is offline. */
    QByteArray name;
    PurplePlugin *prpl = purple_find_prpl(purple_account_get_protocol_id(account));
    PurplePluginProtocolInfo *info = prpl ? PURPLE_PLUGIN_PROTOCOL_INFO(prpl) : nullptr;
    if (info && info->get_chat_name) {
        char *n = info->get_chat_name(purple_chat_get_components(chat));
        if (n) {
            name = QByteArray(n);
            g_free(n);
        }
    }
    if (name.isEmpty()) {
        if (const char *n = purple_chat_get_name(chat))
            name = QByteArray(n);
    }
    if (name.isEmpty())
        return;
    showSources(qs(purple_chat_get_name(chat)), { Source{ PURPLE_LOG_CHAT, name, account } });
}

} // namespace LogViewer

/* ========================================================== LogViewerDialog */

LogViewerDialog::LogViewerDialog(const QString &title, const QList<LogViewer::Source> &sources,
                                 QWidget *parent)
    : QDialog(parent), m_sources(sources)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Conversation Log - %1").arg(title));
    resize(780, 500);

    m_summary = new QLabel(this);
    m_summary->setTextFormat(Qt::PlainText);
    m_note = new QLabel(this);
    m_note->setTextFormat(Qt::PlainText);
    m_note->setWordWrap(true);
    m_note->hide();

    m_list = new QTreeWidget(this);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(true);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &LogViewerDialog::onSelectionChanged);

    m_view = new HistoryView(this);
    /* HistoryView keeps the last 3000 lines for live conversations; a log
     * is shown whole. */
    m_view->document()->setMaximumBlockCount(0);
    connect(m_view, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        QtPurple::openLink(url);
    });

    QSplitter *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_list);
    split->addWidget(m_view);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({ 230, 550 });

    m_reload = new QPushButton(tr("&Reload"), this);
    m_reload->setToolTip(tr("Look for logs written since this window was opened"));
    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(m_reload, &QPushButton::clicked, this, &LogViewerDialog::load);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_reload);
    buttons->addStretch(1);
    buttons->addWidget(close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(m_summary);
    lay->addWidget(m_note);
    lay->addWidget(split, 1);
    lay->addLayout(buttons);

    load();
}

LogViewerDialog::~LogViewerDialog()
{
    freeLogs();
}

void LogViewerDialog::freeLogs()
{
    /* After purple_core_quit() the loggers are gone: leave the logs alone. */
    if (!Core::shuttingDown()) {
        for (const Entry &e : std::as_const(m_logs))
            purple_log_free(e.log);
    }
    m_logs.clear();
}

void LogViewerDialog::load()
{
    m_view->clear();
    m_list->clear();
    freeLogs();

    const bool merged = m_sources.size() > 1;
    for (const LogViewer::Source &src : std::as_const(m_sources)) {
        if (!QtPurple::accountExists(src.account))
            continue;
        GList *logs = purple_log_get_logs(src.type, src.name.constData(), src.account);
        for (GList *l = logs; l; l = l->next) {
            Entry e;
            e.log = static_cast<PurpleLog *>(l->data);
            e.who = merged ? QString::fromUtf8(src.name) : QString();
            m_logs.append(e);
        }
        g_list_free(logs);              /* the PurpleLog objects stay ours */
    }

    std::stable_sort(m_logs.begin(), m_logs.end(), [](const Entry &a, const Entry &b) {
        return a.log->time > b.log->time;
    });

    /* Month rows with one row per log below, newest first. */
    QHash<QString, QTreeWidgetItem *> months;
    QTreeWidgetItem *newest = nullptr;
    qint64 totalBytes = 0;
    for (int i = 0; i < m_logs.size(); ++i) {
        PurpleLog *log = m_logs.at(i).log;
        const QDateTime when = QDateTime::fromSecsSinceEpoch(qint64(log->time));
        const QString monthKey = when.toString(QStringLiteral("yyyy-MM"));
        QTreeWidgetItem *month = months.value(monthKey, nullptr);
        if (!month) {
            month = new QTreeWidgetItem(m_list);
            month->setText(0, QLocale().toString(when.date(), QStringLiteral("MMMM yyyy")));
            month->setFlags(Qt::ItemIsEnabled);
            QFont f = month->font(0);
            f.setBold(true);
            month->setFont(0, f);
            months.insert(monthKey, month);
        }
        const int bytes = purple_log_get_size(log);
        totalBytes += bytes > 0 ? bytes : 0;
        QString text = QLocale().toString(when, QLocale::ShortFormat);
        if (!m_logs.at(i).who.isEmpty())
            text += QStringLiteral("  [%1]").arg(m_logs.at(i).who);
        QTreeWidgetItem *item = new QTreeWidgetItem(month);
        item->setText(0, text);
        item->setToolTip(0, tr("%1, %2").arg(when.toString(Qt::TextDate), sizeText(bytes)));
        item->setData(0, RoleLogIndex, i);
        if (!newest)
            newest = item;
    }

    m_summary->setText(m_logs.isEmpty()
                           ? tr("No saved logs")
                           : tr("%n log(s), %1 in total", nullptr, m_logs.size()).arg(sizeText(totalBytes)));

    /* Tell the user when nothing is being saved. */
    const PurpleLogType type = m_sources.first().type;
    if (!loggingEnabled(type)) {
        m_note->setText(type == PURPLE_LOG_CHAT
            ? tr("Logging of chats is switched off (Tools > Preferences > Logging): new messages are not being saved.")
            : tr("Logging of instant messages is switched off (Tools > Preferences > Logging): new messages are not being saved."));
        m_note->show();
    } else {
        m_note->hide();
    }

    if (newest) {
        newest->parent()->setExpanded(true);
        m_list->setCurrentItem(newest);     /* shows it via onSelectionChanged() */
    } else {
        m_view->setPlainText(tr("There are no saved logs for this conversation yet."));
    }
}

void LogViewerDialog::onSelectionChanged()
{
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item || !item->data(0, RoleLogIndex).isValid()) {
        m_view->clear();
        return;
    }
    const int index = item->data(0, RoleLogIndex).toInt();
    if (index < 0 || index >= m_logs.size())
        return;

    PurpleLogReadFlags flags = static_cast<PurpleLogReadFlags>(0);
    char *text = purple_log_read(m_logs.at(index).log, &flags);
    QString html = qs(text);
    g_free(text);
    if (!(flags & PURPLE_LOG_READ_NO_NEWLINE))
        html.replace(QLatin1Char('\n'), QLatin1String("<br>"));

    /* Same treatment as the live view: remote images never load, ":code:"
     * becomes the emoji. Links are opened through QtPurple::openLink(). */
    m_view->setHtml(Emoji::render(html, purple_prefs_get_bool(QTPURPLE_PREF_EMOJI_CODES)));
}
