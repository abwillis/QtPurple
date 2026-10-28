/*
 * QtPurple - a Qt front end for libpurple
 *
 * PurpleRequestUiOps implementation.
 */
#include "requestui.h"

#include "blistdialogs.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstdarg>
#include <cstring>

/* Handle -> "close it" function, used by close_request(). */
static QHash<void *, std::function<void()>> s_closers;

static QByteArray glibFileName(const QString &path)
{
#ifdef _WIN32
    return path.toUtf8();               /* libpurple on Windows uses UTF-8 */
#else
    return QFile::encodeName(path);
#endif
}

static QString dialogTitle(const char *title)
{
    return (title && *title) ? qs(title) : QStringLiteral(QTPURPLE_NAME);
}

/* ======================================================== RequestDialog */

RequestDialog::RequestDialog(PurpleRequestType type, QWidget *parent)
    : QDialog(parent), m_type(type)
{
    setModal(false);
    s_closers.insert(this, [this]() { closeFromCore(); });
}

RequestDialog::~RequestDialog()
{
    s_closers.remove(this);
}

void RequestDialog::closeFromCore()
{
    if (m_closed)
        return;
    m_closed = true;
    m_done = true;
    s_closers.remove(this);
    hide();
    deleteLater();
}

void RequestDialog::complete(const std::function<void()> &callback)
{
    if (m_done)
        return;
    m_done = true;
    hide();
    if (callback)
        callback();
    /* Normally ends up in closeFromCore() via close_request(). */
    purple_request_close(m_type, this);
    if (!m_closed)
        closeFromCore();
}

void RequestDialog::reject()
{
    if (!m_done)
        cancelled();
    else
        QDialog::reject();
}

QWidget *RequestDialog::makeHeader(const char *primary, const char *secondary,
                                   PurpleAccount *account, const QPixmap &icon)
{
    QWidget *w = new QWidget(this);
    QHBoxLayout *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);

    if (!icon.isNull()) {
        QLabel *il = new QLabel(w);
        il->setPixmap(icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
        v->addWidget(p);
    }
    if (secondary && *secondary) {
        QLabel *s = new QLabel(qs(secondary), w);
        s->setTextFormat(Qt::PlainText);
        s->setWordWrap(true);
        s->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(s);
    }
    if (account) {
        QLabel *a = new QLabel(tr("Account: %1").arg(QtPurple::accountDisplayName(account)), w);
        a->setTextFormat(Qt::PlainText);
        QFont f = a->font();
        f.setItalic(true);
        a->setFont(f);
        v->addWidget(a);
    }
    h->addLayout(v, 1);
    return w;
}

/* ========================================================= InputRequest */

namespace {

class InputRequest : public RequestDialog
{
public:
    InputRequest(const char *title, const char *primary, const char *secondary,
                 const char *defaultValue, bool multiline, bool masked, const char *hint,
                 const char *okText, GCallback okCb, const char *cancelText, GCallback cancelCb,
                 PurpleAccount *account, void *userData)
        : RequestDialog(PURPLE_REQUEST_INPUT), m_okCb(okCb), m_cancelCb(cancelCb),
          m_userData(userData), m_html(hint && strcmp(hint, "html") == 0)
    {
        setWindowTitle(dialogTitle(title));
        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addWidget(makeHeader(primary, secondary, account));

        QString def = qs(defaultValue);
        if (m_html && !def.isEmpty()) {
            gchar *plain = purple_markup_strip_html(defaultValue);
            def = qs(plain);
            g_free(plain);
        }

        if (multiline || m_html) {
            m_multi = new QPlainTextEdit(def, this);
            lay->addWidget(m_multi, 1);
            m_multi->setFocus();
        } else {
            m_line = new QLineEdit(def, this);
            if (masked)
                m_line->setEchoMode(QLineEdit::Password);
            lay->addWidget(m_line);
            m_line->setFocus();
            m_line->selectAll();
        }

        QDialogButtonBox *bb = new QDialogButtonBox(this);
        QPushButton *ok = bb->addButton(okText ? QtPurple::mnemonicToQt(okText) : tr("&OK"),
                                        QDialogButtonBox::AcceptRole);
        bb->addButton(cancelText ? QtPurple::mnemonicToQt(cancelText) : tr("&Cancel"),
                      QDialogButtonBox::RejectRole);
        ok->setDefault(true);
        connect(bb, &QDialogButtonBox::accepted, this, [this]() { finish(m_okCb); });
        connect(bb, &QDialogButtonBox::rejected, this, [this]() { finish(m_cancelCb); });
        lay->addWidget(bb);
        resize(380, multiline ? 260 : sizeHint().height());
    }

protected:
    void cancelled() override { finish(m_cancelCb); }

private:
    void finish(GCallback cb)
    {
        QString text = m_multi ? m_multi->toPlainText() : m_line->text();
        if (m_html) {
            text = text.toHtmlEscaped();
            text.replace(QLatin1Char('\n'), QLatin1String("<br>"));
        }
        const QByteArray value = text.toUtf8();
        void *ud = m_userData;
        complete([cb, value, ud]() {
            if (cb)
                reinterpret_cast<PurpleRequestInputCb>(cb)(ud, value.constData());
        });
    }

    GCallback m_okCb;
    GCallback m_cancelCb;
    void *m_userData;
    bool m_html;
    QLineEdit *m_line = nullptr;
    QPlainTextEdit *m_multi = nullptr;
};

/* ======================================================== ChoiceRequest */

class ChoiceRequest : public RequestDialog
{
public:
    ChoiceRequest(const char *title, const char *primary, const char *secondary, int defaultValue,
                  const char *okText, GCallback okCb, const char *cancelText, GCallback cancelCb,
                  PurpleAccount *account, void *userData, va_list choices)
        : RequestDialog(PURPLE_REQUEST_CHOICE), m_okCb(okCb), m_cancelCb(cancelCb),
          m_userData(userData), m_default(defaultValue)
    {
        setWindowTitle(dialogTitle(title));
        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addWidget(makeHeader(primary, secondary, account));

        m_group = new QButtonGroup(this);
        const char *label;
        while ((label = va_arg(choices, const char *)) != nullptr) {
            const int value = va_arg(choices, int);
            QRadioButton *rb = new QRadioButton(QtPurple::mnemonicToQt(label), this);
            m_group->addButton(rb, value);
            lay->addWidget(rb);
            if (value == defaultValue)
                rb->setChecked(true);
        }
        if (!m_group->checkedButton() && !m_group->buttons().isEmpty())
            m_group->buttons().first()->setChecked(true);

        QDialogButtonBox *bb = new QDialogButtonBox(this);
        QPushButton *ok = bb->addButton(okText ? QtPurple::mnemonicToQt(okText) : tr("&OK"),
                                        QDialogButtonBox::AcceptRole);
        bb->addButton(cancelText ? QtPurple::mnemonicToQt(cancelText) : tr("&Cancel"),
                      QDialogButtonBox::RejectRole);
        ok->setDefault(true);
        connect(bb, &QDialogButtonBox::accepted, this, [this]() { finish(m_okCb); });
        connect(bb, &QDialogButtonBox::rejected, this, [this]() { finish(m_cancelCb); });
        lay->addWidget(bb);
    }

protected:
    void cancelled() override { finish(m_cancelCb); }

private:
    void finish(GCallback cb)
    {
        const int value = m_group->checkedButton() ? m_group->checkedId() : m_default;
        void *ud = m_userData;
        complete([cb, value, ud]() {
            if (cb)
                reinterpret_cast<PurpleRequestChoiceCb>(cb)(ud, value);
        });
    }

    GCallback m_okCb;
    GCallback m_cancelCb;
    void *m_userData;
    int m_default;
    QButtonGroup *m_group;
};

/* ======================================================== ActionRequest */

class ActionRequest : public RequestDialog
{
public:
    ActionRequest(const char *title, const char *primary, const char *secondary, int defaultAction,
                  PurpleAccount *account, void *userData, size_t count, va_list actions,
                  const QPixmap &icon = QPixmap())
        : RequestDialog(PURPLE_REQUEST_ACTION), m_userData(userData)
    {
        setWindowTitle(dialogTitle(title));
        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addWidget(makeHeader(primary, secondary, account, icon));

        QHBoxLayout *buttons = new QHBoxLayout;
        buttons->addStretch(1);
        for (size_t i = 0; i < count; ++i) {
            const char *text = va_arg(actions, const char *);
            GCallback cb = va_arg(actions, GCallback);
            m_callbacks.append(cb);
            QPushButton *b = new QPushButton(QtPurple::mnemonicToQt(text), this);
            b->setAutoDefault(false);
            const int index = int(i);
            connect(b, &QPushButton::clicked, this, [this, index]() { finish(index); });
            if (index == defaultAction) {
                b->setDefault(true);
                b->setFocus();
            }
            buttons->addWidget(b);
        }
        lay->addLayout(buttons);
    }

protected:
    /* Closing the window picks the last action: libpurple callers put the
     * negative choice ("Cancel", "No", "Reject") last, and running no
     * callback at all would leave e.g. certificate prompts hanging. */
    void cancelled() override { finish(m_callbacks.size() - 1); }

private:
    void finish(int index)
    {
        if (index < 0 || index >= m_callbacks.size()) {
            complete(nullptr);
            return;
        }
        GCallback cb = m_callbacks.at(index);
        void *ud = m_userData;
        complete([cb, index, ud]() {
            if (cb)
                reinterpret_cast<PurpleRequestActionCb>(cb)(ud, index);
        });
    }

    void *m_userData;
    QList<GCallback> m_callbacks;
};

/* ======================================================== FieldsRequest */

class FieldsRequest : public RequestDialog
{
public:
    FieldsRequest(const char *title, const char *primary, const char *secondary,
                  PurpleRequestFields *fields, const char *okText, GCallback okCb,
                  const char *cancelText, GCallback cancelCb, PurpleAccount *account,
                  void *userData)
        : RequestDialog(PURPLE_REQUEST_FIELDS), m_fields(fields), m_okCb(okCb),
          m_cancelCb(cancelCb), m_userData(userData)
    {
        setWindowTitle(dialogTitle(title));
        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addWidget(makeHeader(primary, secondary, account));

        QWidget *body = new QWidget;
        QVBoxLayout *bodyLay = new QVBoxLayout(body);
        bodyLay->setContentsMargins(0, 0, 0, 0);
        int fieldCount = 0;

        for (GList *g = purple_request_fields_get_groups(fields); g; g = g->next) {
            PurpleRequestFieldGroup *group = static_cast<PurpleRequestFieldGroup *>(g->data);
            QFormLayout *form = new QFormLayout;
            const char *gtitle = purple_request_field_group_get_title(group);
            if (gtitle && *gtitle) {
                QGroupBox *box = new QGroupBox(qs(gtitle), body);
                box->setLayout(form);
                bodyLay->addWidget(box);
            } else {
                bodyLay->addLayout(form);
            }
            for (GList *f = purple_request_field_group_get_fields(group); f; f = f->next) {
                addField(form, static_cast<PurpleRequestField *>(f->data), body);
                ++fieldCount;
            }
        }
        bodyLay->addStretch(1);

        if (fieldCount > 8) {
            QScrollArea *scroll = new QScrollArea(this);
            scroll->setWidgetResizable(true);
            scroll->setWidget(body);
            lay->addWidget(scroll, 1);
            resize(460, 520);
        } else {
            body->setParent(this);
            lay->addWidget(body, 1);
        }

        QDialogButtonBox *bb = new QDialogButtonBox(this);
        m_ok = bb->addButton(okText ? QtPurple::mnemonicToQt(okText) : tr("&OK"),
                             QDialogButtonBox::AcceptRole);
        bb->addButton(cancelText ? QtPurple::mnemonicToQt(cancelText) : tr("&Cancel"),
                      QDialogButtonBox::RejectRole);
        m_ok->setDefault(true);
        connect(bb, &QDialogButtonBox::accepted, this, [this]() { finish(m_okCb); });
        connect(bb, &QDialogButtonBox::rejected, this, [this]() { finish(m_cancelCb); });
        lay->addWidget(bb);

        updateOk();
    }

    void closeFromCore() override
    {
        if (m_fields) {
            /* The UI owns the fields once request_fields() was called. */
            purple_request_fields_destroy(m_fields);
            m_fields = nullptr;
        }
        RequestDialog::closeFromCore();
    }

protected:
    void cancelled() override { finish(m_cancelCb); }

private:
    void updateOk()
    {
        if (m_fields && m_ok)
            m_ok->setEnabled(purple_request_fields_all_required_filled(m_fields));
    }

    void addField(QFormLayout *form, PurpleRequestField *field, QWidget *parent)
    {
        if (!purple_request_field_is_visible(field))
            return;

        QString label = QtPurple::mnemonicToQt(purple_request_field_get_label(field));
        if (purple_request_field_is_required(field) && !label.isEmpty())
            label += QLatin1Char('*');

        switch (purple_request_field_get_type(field)) {
        case PURPLE_REQUEST_FIELD_STRING: {
            const QString def = qs(purple_request_field_string_get_default_value(field));
            if (purple_request_field_string_is_multiline(field)) {
                QPlainTextEdit *e = new QPlainTextEdit(def, parent);
                e->setReadOnly(!purple_request_field_string_is_editable(field));
                e->setMinimumHeight(fontMetrics().lineSpacing() * 4);
                connect(e, &QPlainTextEdit::textChanged, this, [this, e, field]() {
                    purple_request_field_string_set_value(field, Utf8(e->toPlainText()));
                    updateOk();
                });
                form->addRow(label, e);
            } else {
                QLineEdit *e = new QLineEdit(def, parent);
                e->setReadOnly(!purple_request_field_string_is_editable(field));
                if (purple_request_field_string_is_masked(field))
                    e->setEchoMode(QLineEdit::Password);
                const char *hint = purple_request_field_get_type_hint(field);
                if (hint && strcmp(hint, "group") == 0) {
                    QStringList groups;
                    for (PurpleBlistNode *n = purple_blist_get_root(); n;
                         n = purple_blist_node_get_sibling_next(n))
                        if (PURPLE_BLIST_NODE_IS_GROUP(n))
                            groups << qs(purple_group_get_name(PURPLE_GROUP(n)));
                    e->setCompleter(new QCompleter(groups, e));
                }
                connect(e, &QLineEdit::textChanged, this, [this, field](const QString &t) {
                    purple_request_field_string_set_value(field, Utf8(t));
                    updateOk();
                });
                form->addRow(label, e);
            }
            break;
        }
        case PURPLE_REQUEST_FIELD_INTEGER: {
            QLineEdit *e = new QLineEdit(QString::number(purple_request_field_int_get_default_value(field)), parent);
            e->setValidator(new QIntValidator(e));
            connect(e, &QLineEdit::textChanged, this, [this, field](const QString &t) {
                purple_request_field_int_set_value(field, t.toInt());
                updateOk();
            });
            form->addRow(label, e);
            break;
        }
        case PURPLE_REQUEST_FIELD_BOOLEAN: {
            QCheckBox *c = new QCheckBox(QtPurple::mnemonicToQt(purple_request_field_get_label(field)), parent);
            c->setChecked(purple_request_field_bool_get_default_value(field));
            connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
                purple_request_field_bool_set_value(field, on ? TRUE : FALSE);
                updateOk();
            });
            form->addRow(c);
            break;
        }
        case PURPLE_REQUEST_FIELD_CHOICE: {
            QComboBox *c = new QComboBox(parent);
            for (GList *l = purple_request_field_choice_get_labels(field); l; l = l->next)
                c->addItem(QtPurple::stripMnemonic(static_cast<const char *>(l->data)));
            c->setCurrentIndex(purple_request_field_choice_get_default_value(field));
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, field](int i) {
                purple_request_field_choice_set_value(field, i);
                updateOk();
            });
            form->addRow(label, c);
            break;
        }
        case PURPLE_REQUEST_FIELD_LIST: {
            QListWidget *list = new QListWidget(parent);
            list->setSelectionMode(purple_request_field_list_get_multi_select(field)
                                       ? QAbstractItemView::MultiSelection
                                       : QAbstractItemView::SingleSelection);
            for (GList *l = purple_request_field_list_get_items(field); l; l = l->next) {
                const char *text = static_cast<const char *>(l->data);
                QListWidgetItem *it = new QListWidgetItem(qs(text), list);
                it->setData(Qt::UserRole, QByteArray(text));
                if (purple_request_field_list_is_selected(field, text))
                    it->setSelected(true);
            }
            list->setMinimumHeight(fontMetrics().lineSpacing() * 5);
            connect(list, &QListWidget::itemSelectionChanged, this, [this, list, field]() {
                QList<QByteArray> keep;
                GList *sel = nullptr;
                for (QListWidgetItem *it : list->selectedItems())
                    keep << it->data(Qt::UserRole).toByteArray();
                for (const QByteArray &b : keep)
                    sel = g_list_append(sel, const_cast<char *>(b.constData()));
                purple_request_field_list_set_selected(field, sel);   /* copies the strings */
                g_list_free(sel);
                updateOk();
            });
            form->addRow(label, list);
            break;
        }
        case PURPLE_REQUEST_FIELD_LABEL: {
            QLabel *l = new QLabel(qs(purple_request_field_get_label(field)), parent);
            l->setTextFormat(Qt::PlainText);
            l->setWordWrap(true);
            form->addRow(l);
            break;
        }
        case PURPLE_REQUEST_FIELD_IMAGE: {
            QPixmap pm;
            pm.loadFromData(reinterpret_cast<const uchar *>(purple_request_field_image_get_buffer(field)),
                            uint(purple_request_field_image_get_size(field)));
            const unsigned sx = purple_request_field_image_get_scale_x(field);
            const unsigned sy = purple_request_field_image_get_scale_y(field);
            if (!pm.isNull() && (sx > 1 || sy > 1))
                pm = pm.scaled(int(pm.width() * qMax(1u, sx)), int(pm.height() * qMax(1u, sy)));
            QLabel *l = new QLabel(parent);
            l->setPixmap(pm);
            form->addRow(label, l);
            break;
        }
        case PURPLE_REQUEST_FIELD_ACCOUNT: {
            const bool showAll = purple_request_field_account_get_show_all(field);
            PurpleFilterAccountFunc filter = purple_request_field_account_get_filter(field);
            AccountCombo *c = new AccountCombo(!showAll, parent, [filter](PurpleAccount *a) {
                return !filter || filter(a);
            });
            PurpleAccount *def = purple_request_field_account_get_default_value(field);
            if (def)
                c->setAccount(def);
            purple_request_field_account_set_value(field, c->account());
            connect(c, &AccountCombo::accountChanged, this, [this, field](PurpleAccount *a) {
                purple_request_field_account_set_value(field, a);
                updateOk();
            });
            form->addRow(label, c);
            break;
        }
        default:
            break;
        }
    }

    void finish(GCallback cb)
    {
        PurpleRequestFields *fields = m_fields;
        void *ud = m_userData;
        complete([cb, fields, ud]() {
            if (cb)
                reinterpret_cast<PurpleRequestFieldsCb>(cb)(ud, fields);
        });
    }

    PurpleRequestFields *m_fields;
    GCallback m_okCb;
    GCallback m_cancelCb;
    void *m_userData;
    QPushButton *m_ok = nullptr;
};

} // namespace

/* ========================================================== FileRequest */

FileRequest::FileRequest(PurpleRequestType type, const char *title, const char *path, bool save,
                         GCallback okCb, GCallback cancelCb, void *userData)
    : QFileDialog(nullptr, dialogTitle(title), qs(path)),
      m_type(type), m_okCb(okCb), m_cancelCb(cancelCb), m_userData(userData)
{
    setModal(false);
    if (type == PURPLE_REQUEST_FOLDER) {
        setFileMode(QFileDialog::Directory);
        setOption(QFileDialog::ShowDirsOnly, true);
    } else if (save) {
        setAcceptMode(QFileDialog::AcceptSave);
        setFileMode(QFileDialog::AnyFile);
        if (path && *path)
            selectFile(qs(path));
    } else {
        setFileMode(QFileDialog::ExistingFile);
    }
    s_closers.insert(this, [this]() { closeFromCore(); });
}

FileRequest::~FileRequest()
{
    s_closers.remove(this);
}

void FileRequest::closeFromCore()
{
    if (m_closed)
        return;
    m_closed = true;
    m_finished = true;
    s_closers.remove(this);
    hide();
    deleteLater();
}

void FileRequest::done(int result)
{
    if (m_finished) {
        QFileDialog::done(result);
        return;
    }
    m_finished = true;

    const QStringList files = selectedFiles();
    const QString path = (result == QDialog::Accepted && !files.isEmpty()) ? files.first() : QString();
    QFileDialog::done(result);

    if (!path.isEmpty()) {
        if (m_okCb)
            reinterpret_cast<PurpleRequestFileCb>(m_okCb)(m_userData, glibFileName(path).constData());
    } else if (m_cancelCb) {
        reinterpret_cast<PurpleRequestFileCb>(m_cancelCb)(m_userData, nullptr);
    }
    purple_request_close(m_type, this);
    if (!m_closed)
        closeFromCore();
}

/* =============================================================== ui ops */

static void *requestInput(const char *title, const char *primary, const char *secondary,
                          const char *default_value, gboolean multiline, gboolean masked,
                          gchar *hint, const char *ok_text, GCallback ok_cb,
                          const char *cancel_text, GCallback cancel_cb, PurpleAccount *account,
                          const char *, PurpleConversation *, void *user_data)
{
    InputRequest *dlg = new InputRequest(title, primary, secondary, default_value, multiline,
                                         masked, hint, ok_text, ok_cb, cancel_text, cancel_cb,
                                         account, user_data);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void *requestChoice(const char *title, const char *primary, const char *secondary,
                           int default_value, const char *ok_text, GCallback ok_cb,
                           const char *cancel_text, GCallback cancel_cb, PurpleAccount *account,
                           const char *, PurpleConversation *, void *user_data, va_list choices)
{
    ChoiceRequest *dlg = new ChoiceRequest(title, primary, secondary, default_value, ok_text, ok_cb,
                                           cancel_text, cancel_cb, account, user_data, choices);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void *requestAction(const char *title, const char *primary, const char *secondary,
                           int default_action, PurpleAccount *account, const char *,
                           PurpleConversation *, void *user_data, size_t action_count,
                           va_list actions)
{
    ActionRequest *dlg = new ActionRequest(title, primary, secondary, default_action, account,
                                           user_data, action_count, actions);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void *requestActionWithIcon(const char *title, const char *primary, const char *secondary,
                                   int default_action, PurpleAccount *account, const char *,
                                   PurpleConversation *, gconstpointer icon_data, gsize icon_size,
                                   void *user_data, size_t action_count, va_list actions)
{
    QPixmap icon;
    if (icon_data && icon_size > 0)
        icon.loadFromData(static_cast<const uchar *>(icon_data), uint(icon_size));
    ActionRequest *dlg = new ActionRequest(title, primary, secondary, default_action, account,
                                           user_data, action_count, actions, icon);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void *requestFields(const char *title, const char *primary, const char *secondary,
                           PurpleRequestFields *fields, const char *ok_text, GCallback ok_cb,
                           const char *cancel_text, GCallback cancel_cb, PurpleAccount *account,
                           const char *, PurpleConversation *, void *user_data)
{
    FieldsRequest *dlg = new FieldsRequest(title, primary, secondary, fields, ok_text, ok_cb,
                                           cancel_text, cancel_cb, account, user_data);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void *requestFile(const char *title, const char *filename, gboolean savedialog,
                         GCallback ok_cb, GCallback cancel_cb, PurpleAccount *,
                         const char *, PurpleConversation *, void *user_data)
{
    FileRequest *dlg = new FileRequest(PURPLE_REQUEST_FILE, title, filename, savedialog,
                                       ok_cb, cancel_cb, user_data);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void *requestFolder(const char *title, const char *dirname, GCallback ok_cb,
                           GCallback cancel_cb, PurpleAccount *, const char *,
                           PurpleConversation *, void *user_data)
{
    FileRequest *dlg = new FileRequest(PURPLE_REQUEST_FOLDER, title, dirname, false,
                                       ok_cb, cancel_cb, user_data);
    dlg->show();
    dlg->raise();
    return dlg;
}

static void closeRequest(PurpleRequestType, void *ui_handle)
{
    auto closer = s_closers.take(ui_handle);
    if (closer)
        closer();
}

PurpleRequestUiOps *RequestUi::uiOps()
{
    static PurpleRequestUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.request_input = requestInput;
        ops.request_choice = requestChoice;
        ops.request_action = requestAction;
        ops.request_fields = requestFields;
        ops.request_file = requestFile;
        ops.close_request = closeRequest;
        ops.request_folder = requestFolder;
        ops.request_action_with_icon = requestActionWithIcon;
        /* request_screenshare_media: not supported (libpurple handles NULL). */
        ready = true;
    }
    return &ops;
}
