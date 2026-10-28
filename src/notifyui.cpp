/*
 * QtPurple - a Qt front end for libpurple
 *
 * PurpleNotifyUiOps implementation.
 */
#include "notifyui.h"

#include "conversationwindow.h"     /* HistoryView */

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>
#include <functional>

static QHash<void *, std::function<void()>> s_closers;

static QString dialogTitle(const char *title)
{
    return (title && *title) ? qs(title) : QStringLiteral(QTPURPLE_NAME);
}

/* ========================================================= NotifyDialog */

NotifyDialog::NotifyDialog(PurpleNotifyType type, QWidget *parent)
    : QDialog(parent), m_type(type)
{
    setModal(false);
    s_closers.insert(this, [this]() { closeFromCore(); });
}

NotifyDialog::~NotifyDialog()
{
    s_closers.remove(this);
}

void NotifyDialog::closeFromCore()
{
    if (m_deleted)
        return;
    m_deleted = true;
    m_closing = true;
    s_closers.remove(this);
    hide();
    deleteLater();
}

void NotifyDialog::done(int result)
{
    QDialog::done(result);
    if (m_closing)
        return;
    m_closing = true;
    /* Calls close_notify() -> closeFromCore(), then libpurple's callback. */
    purple_notify_close(m_type, this);
    if (!m_deleted)
        closeFromCore();
}

/* Body widgets ---------------------------------------------------------- */

static QWidget *makeTextHeader(QWidget *parent, const QIcon &icon, const char *primary,
                               const char *secondary)
{
    QWidget *w = new QWidget(parent);
    QHBoxLayout *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    if (!icon.isNull()) {
        QLabel *il = new QLabel(w);
        il->setPixmap(icon.pixmap(48, 48));
        il->setAlignment(Qt::AlignTop);
        h->addWidget(il);
    }
    QVBoxLayout *v = new QVBoxLayout;
    if (primary && *primary) {
        QLabel *p = new QLabel(qs(primary), w);
        p->setTextFormat(Qt::PlainText);
        p->setWordWrap(true);
        QFont f = p->font();
        f.setBold(true);
        p->setFont(f);
        p->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(p);
    }
    if (secondary && *secondary) {
        QLabel *s = new QLabel(qs(secondary), w);
        s->setTextFormat(Qt::PlainText);
        s->setWordWrap(true);
        s->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(s);
    }
    v->addStretch(1);
    h->addLayout(v, 1);
    return w;
}

static QWidget *makeHtmlBody(QWidget *parent, const QString &html)
{
    HistoryView *view = new HistoryView(parent);
    view->setHtml(html);
    QObject::connect(view, &QTextBrowser::anchorClicked, view, [](const QUrl &url) {
        QtPurple::openLink(url);
    });
    return view;
}

static NotifyDialog *makeSimpleDialog(PurpleNotifyType type, const QString &title, const QIcon &icon,
                                      const char *primary, const char *secondary,
                                      const QString &html = QString(),
                                      const QString &openUrl = QString())
{
    NotifyDialog *dlg = new NotifyDialog(type);
    dlg->setWindowTitle(title);
    QVBoxLayout *lay = new QVBoxLayout(dlg);
    lay->addWidget(makeTextHeader(dlg, icon, primary, secondary));
    if (!html.isEmpty())
        lay->addWidget(makeHtmlBody(dlg, html), 1);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    if (!openUrl.isEmpty()) {
        QPushButton *open = bb->addButton(NotifyDialog::tr("&Open"), QDialogButtonBox::ActionRole);
        QObject::connect(open, &QPushButton::clicked, dlg, [openUrl]() {
            QtPurple::openLink(QUrl(openUrl));
        });
    }
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    QObject::connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    lay->addWidget(bb);

    if (!html.isEmpty())
        dlg->resize(460, 420);
    dlg->show();
    dlg->raise();
    return dlg;
}

/* ================================================== SearchResultsDialog */

static QString buttonLabel(const PurpleNotifySearchButton *button)
{
    switch (button->type) {
    case PURPLE_NOTIFY_BUTTON_LABELED:  return QtPurple::mnemonicToQt(button->label);
    case PURPLE_NOTIFY_BUTTON_CONTINUE: return SearchResultsDialog::tr("Co&ntinue");
    case PURPLE_NOTIFY_BUTTON_ADD:      return SearchResultsDialog::tr("&Add");
    case PURPLE_NOTIFY_BUTTON_INFO:     return SearchResultsDialog::tr("Get &Info");
    case PURPLE_NOTIFY_BUTTON_IM:       return SearchResultsDialog::tr("&Send Message");
    case PURPLE_NOTIFY_BUTTON_JOIN:     return SearchResultsDialog::tr("&Join");
    case PURPLE_NOTIFY_BUTTON_INVITE:   return SearchResultsDialog::tr("In&vite");
    }
    return QString();
}

SearchResultsDialog::SearchResultsDialog(PurpleConnection *gc, const char *title,
                                         const char *primary, const char *secondary,
                                         PurpleNotifySearchResults *results, gpointer userData)
    : NotifyDialog(PURPLE_NOTIFY_SEARCHRESULTS),
      m_account(purple_connection_get_account(gc)), m_results(results), m_userData(userData)
{
    setWindowTitle(dialogTitle(title));
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(makeTextHeader(this, QIcon(), primary, secondary));

    m_tree = new QTreeWidget(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    QStringList headers;
    for (GList *c = results->columns; c; c = c->next)
        headers << qs(static_cast<PurpleNotifySearchColumn *>(c->data)->title);
    m_tree->setColumnCount(headers.size());
    m_tree->setHeaderLabels(headers);
    lay->addWidget(m_tree, 1);

    QHBoxLayout *buttons = new QHBoxLayout;
    for (GList *b = results->buttons; b; b = b->next) {
        PurpleNotifySearchButton *button = static_cast<PurpleNotifySearchButton *>(b->data);
        QPushButton *pb = new QPushButton(buttonLabel(button), this);
        pb->setAutoDefault(false);
        connect(pb, &QPushButton::clicked, this, [this, button]() {
            if (!QtPurple::accountExists(m_account))
                return;
            PurpleConnection *conn = purple_account_get_connection(m_account);
            if (!conn || !button->callback)
                return;
            /* Like Pidgin: pass a freshly allocated copy of the selected row. */
            GList *row = nullptr;
            QTreeWidgetItem *item = m_tree->currentItem();
            if (item) {
                for (int i = 0; i < m_tree->columnCount(); ++i)
                    row = g_list_append(row, g_strdup(item->text(i).toUtf8().constData()));
            }
            button->callback(conn, row, m_userData);
            g_list_free_full(row, g_free);
        });
        buttons->addWidget(pb);
    }
    buttons->addStretch(1);
    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(close);
    lay->addLayout(buttons);

    setRows(results);
    resize(620, 420);
}

void SearchResultsDialog::setRows(PurpleNotifySearchResults *results)
{
    m_tree->clear();
    for (GList *r = results->rows; r; r = r->next) {
        QStringList cols;
        for (GList *c = static_cast<GList *>(r->data); c; c = c->next)
            cols << qs(static_cast<const char *>(c->data));
        new QTreeWidgetItem(m_tree, cols);
    }
    for (int i = 0; i < m_tree->columnCount(); ++i)
        m_tree->resizeColumnToContents(i);
}

void SearchResultsDialog::closeFromCore()
{
    if (m_results) {
        purple_notify_searchresults_free(m_results);
        m_results = nullptr;
    }
    NotifyDialog::closeFromCore();
}

/* =============================================================== ui ops */

static void *notifyMessage(PurpleNotifyMsgType type, const char *title,
                           const char *primary, const char *secondary)
{
    QStyle::StandardPixmap sp = QStyle::SP_MessageBoxInformation;
    if (type == PURPLE_NOTIFY_MSG_ERROR)
        sp = QStyle::SP_MessageBoxCritical;
    else if (type == PURPLE_NOTIFY_MSG_WARNING)
        sp = QStyle::SP_MessageBoxWarning;
    return makeSimpleDialog(PURPLE_NOTIFY_MESSAGE, dialogTitle(title),
                            QApplication::style()->standardIcon(sp), primary, secondary);
}

static void *notifyEmail(PurpleConnection *gc, const char *subject, const char *from,
                         const char *to, const char *url)
{
    PurpleAccount *account = purple_connection_get_account(gc);
    const QString text = NotifyDialog::tr("From: %1\nTo: %2\nSubject: %3")
        .arg(qs(from), qs(to && *to ? to : purple_account_get_username(account)), qs(subject));
    const QByteArray secondary = text.toUtf8();
    return makeSimpleDialog(PURPLE_NOTIFY_EMAIL, NotifyDialog::tr("New Mail"),
                            QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation),
                            NotifyDialog::tr("You have new mail!").toUtf8().constData(),
                            secondary.constData(), QString(), qs(url));
}

static void *notifyEmails(PurpleConnection *gc, size_t count, gboolean detailed,
                          const char **subjects, const char **froms, const char **tos,
                          const char **urls)
{
    PurpleAccount *account = purple_connection_get_account(gc);
    const QString primary = NotifyDialog::tr("%1 has %n new message(s).", nullptr, int(count))
        .arg(QtPurple::accountDisplayName(account));
    QString secondary;
    if (detailed && subjects) {
        for (size_t i = 0; i < count; ++i) {
            secondary += NotifyDialog::tr("%1 \u2014 %2\n")
                .arg(qs(froms ? froms[i] : nullptr), qs(subjects[i]));
        }
    }
    Q_UNUSED(tos);
    const QByteArray p = primary.toUtf8();
    const QByteArray s = secondary.trimmed().toUtf8();
    return makeSimpleDialog(PURPLE_NOTIFY_EMAILS, NotifyDialog::tr("New Mail"),
                            QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation),
                            p.constData(), s.isEmpty() ? nullptr : s.constData(), QString(),
                            (urls && urls[0]) ? qs(urls[0]) : QString());
}

static void *notifyFormatted(const char *title, const char *primary, const char *secondary,
                             const char *text)
{
    return makeSimpleDialog(PURPLE_NOTIFY_FORMATTED, dialogTitle(title), QIcon(),
                            primary, secondary, qs(text));
}

static void *notifySearchResults(PurpleConnection *gc, const char *title, const char *primary,
                                 const char *secondary, PurpleNotifySearchResults *results,
                                 gpointer user_data)
{
    SearchResultsDialog *dlg = new SearchResultsDialog(gc, title, primary, secondary,
                                                       results, user_data);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void notifySearchResultsNewRows(PurpleConnection *, PurpleNotifySearchResults *results,
                                       void *data)
{
    if (!s_closers.contains(data))
        return;
    static_cast<SearchResultsDialog *>(data)->setRows(results);
}

static void *notifyUserInfo(PurpleConnection *gc, const char *who, PurpleNotifyUserInfo *user_info)
{
    char *text = purple_notify_user_info_get_text_with_newline(user_info, "<br />");
    const QString html = qs(text);
    g_free(text);
    PurpleAccount *account = purple_connection_get_account(gc);
    const QByteArray secondary = QtPurple::accountDisplayName(account).toUtf8();
    const QByteArray primary = NotifyDialog::tr("Info for %1").arg(qs(who)).toUtf8();
    return makeSimpleDialog(PURPLE_NOTIFY_USERINFO, NotifyDialog::tr("User Info"), QIcon(),
                            primary.constData(), secondary.constData(),
                            html.isEmpty() ? NotifyDialog::tr("(no information)") : html);
}

static void *notifyUri(const char *uri)
{
    const QUrl url(qs(uri));
    if (!QtPurple::openLink(url)) {
        purple_notify_error(nullptr, NotifyDialog::tr("Open Link").toUtf8().constData(),
                            NotifyDialog::tr("Refusing to open this link.").toUtf8().constData(),
                            uri);
    }
    return nullptr;
}

static void closeNotify(PurpleNotifyType, void *ui_handle)
{
    auto closer = s_closers.take(ui_handle);
    if (closer)
        closer();
}

PurpleNotifyUiOps *NotifyUi::uiOps()
{
    static PurpleNotifyUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.notify_message = notifyMessage;
        ops.notify_email = notifyEmail;
        ops.notify_emails = notifyEmails;
        ops.notify_formatted = notifyFormatted;
        ops.notify_searchresults = notifySearchResults;
        ops.notify_searchresults_new_rows = notifySearchResultsNewRows;
        ops.notify_userinfo = notifyUserInfo;
        ops.notify_uri = notifyUri;
        ops.close_notify = closeNotify;
        ready = true;
    }
    return &ops;
}
