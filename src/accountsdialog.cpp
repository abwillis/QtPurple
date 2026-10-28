/*
 * QtPurple - a Qt front end for libpurple
 *
 * Account manager and account editor.
 */
#include "accountsdialog.h"

#include "core.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cstring>

static QPointer<AccountsDialog> s_dialog;
static QHash<PurpleAccount *, QPointer<AccountEditor>> s_editors;
static QPointer<AccountEditor> s_newEditor;
static int s_signalHandle = 0;

/* ----------------------------------------------------------- signals */

static void accountsChanged(PurpleAccount *, gpointer)
{
    if (s_dialog)
        s_dialog->scheduleRefresh();
}

static void accountRemoved(PurpleAccount *account, gpointer)
{
    auto it = s_editors.find(account);
    if (it != s_editors.end()) {
        if (it.value()) {
            it.value()->hide();
            it.value()->deleteLater();
        }
        s_editors.erase(it);
    }
    if (s_dialog)
        s_dialog->scheduleRefresh();
}

static void ensureSignals()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    void *h = purple_accounts_get_handle();
    const char *names[] = { "account-added", "account-enabled", "account-disabled",
                            "account-signed-on", "account-signed-off", "account-connecting",
                            "account-status-changed", "account-error-changed",
                            "account-alias-changed" };
    for (const char *name : names)
        purple_signal_connect(h, name, &s_signalHandle, PURPLE_CALLBACK(accountsChanged), nullptr);
    purple_signal_connect(h, "account-removed", &s_signalHandle,
                          PURPLE_CALLBACK(accountRemoved), nullptr);
}

/* ======================================================= AccountsDialog */

void AccountsDialog::showDialog()
{
    ensureSignals();
    if (!s_dialog)
        s_dialog = new AccountsDialog();
    s_dialog->refresh();
    s_dialog->show();
    s_dialog->raise();
    s_dialog->activateWindow();
}

void AccountsDialog::editAccount(PurpleAccount *account)
{
    ensureSignals();
    QPointer<AccountEditor> &slot = account ? s_editors[account] : s_newEditor;
    if (!slot) {
        slot = new AccountEditor(account);
        QObject::connect(slot.data(), &QDialog::finished, slot.data(), [](int) {
            if (s_dialog)
                s_dialog->scheduleRefresh();
        });
    }
    slot->show();
    slot->raise();
    slot->activateWindow();
}

AccountsDialog::AccountsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Accounts"));
    resize(560, 320);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({ tr("Enabled"), tr("Username"), tr("Protocol"), tr("Status") });
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::itemChanged, this, &AccountsDialog::onItemChanged);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &AccountsDialog::onSelectionChanged);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { onModify(); });

    QPushButton *add = new QPushButton(tr("&Add..."), this);
    m_modify = new QPushButton(tr("&Modify..."), this);
    m_delete = new QPushButton(tr("&Delete"), this);
    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(add, &QPushButton::clicked, this, &AccountsDialog::onAdd);
    connect(m_modify, &QPushButton::clicked, this, &AccountsDialog::onModify);
    connect(m_delete, &QPushButton::clicked, this, &AccountsDialog::onDelete);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(m_modify);
    buttons->addWidget(m_delete);
    buttons->addStretch(1);
    buttons->addWidget(close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(m_list, 1);
    lay->addLayout(buttons);

    onSelectionChanged();
}

void AccountsDialog::scheduleRefresh()
{
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, &AccountsDialog::refresh);
}

static QString accountStatusText(PurpleAccount *account)
{
    if (!purple_account_get_enabled(account, QTPURPLE_UI_ID))
        return AccountsDialog::tr("Disabled");
    if (purple_account_is_connected(account)) {
        PurpleStatus *status = purple_account_get_active_status(account);
        return status ? qs(purple_status_get_name(status)) : AccountsDialog::tr("Online");
    }
    if (purple_account_is_connecting(account))
        return AccountsDialog::tr("Connecting...");
    const PurpleConnectionErrorInfo *err = purple_account_get_current_error(account);
    if (err && err->description)
        return AccountsDialog::tr("Error: %1").arg(qs(err->description));
    return AccountsDialog::tr("Offline");
}

void AccountsDialog::refresh()
{
    m_refreshPending = false;
    PurpleAccount *selected = selectedAccount();

    m_filling = true;
    m_list->clear();
    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        PurpleAccount *account = static_cast<PurpleAccount *>(l->data);
        QTreeWidgetItem *item = new QTreeWidgetItem(m_list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, purple_account_get_enabled(account, QTPURPLE_UI_ID) ? Qt::Checked : Qt::Unchecked);
        item->setIcon(1, QtPurple::statusIcon(QtPurple::accountPrimitive(account)));
        item->setText(1, qs(purple_account_get_username(account)));
        item->setIcon(2, QtPurple::protocolIcon(account));
        item->setText(2, qs(purple_account_get_protocol_name(account)));
        item->setText(3, accountStatusText(account));
        item->setToolTip(3, item->text(3));
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(account)));
        if (account == selected)
            m_list->setCurrentItem(item);
    }
    m_filling = false;
    onSelectionChanged();
}

PurpleAccount *AccountsDialog::selectedAccount() const
{
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item)
        return nullptr;
    PurpleAccount *account = reinterpret_cast<PurpleAccount *>(item->data(0, Qt::UserRole).value<quintptr>());
    return QtPurple::accountExists(account) ? account : nullptr;
}

void AccountsDialog::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_filling || column != 0)
        return;
    PurpleAccount *account = reinterpret_cast<PurpleAccount *>(item->data(0, Qt::UserRole).value<quintptr>());
    if (!QtPurple::accountExists(account)) {
        scheduleRefresh();
        return;
    }
    const bool enable = item->checkState(0) == Qt::Checked;
    if (bool(purple_account_get_enabled(account, QTPURPLE_UI_ID)) == enable)
        return;

    if (enable) {
        /* Make sure the account comes up with the current global status. */
        PurpleSavedStatus *saved = purple_savedstatus_get_current();
        if (saved)
            purple_savedstatus_activate_for_account(saved, account);
    }
    purple_account_set_enabled(account, QTPURPLE_UI_ID, enable ? TRUE : FALSE);
    scheduleRefresh();
}

void AccountsDialog::onSelectionChanged()
{
    const bool has = selectedAccount() != nullptr;
    m_modify->setEnabled(has);
    m_delete->setEnabled(has);
}

void AccountsDialog::onAdd()
{
    editAccount(nullptr);
}

void AccountsDialog::onModify()
{
    if (PurpleAccount *account = selectedAccount())
        editAccount(account);
}

void AccountsDialog::onDelete()
{
    PurpleAccount *account = selectedAccount();
    if (!account)
        return;
    if (QMessageBox::question(this, tr("Delete Account"),
                              tr("Delete the account %1?\n\nIts buddies and logs on this "
                                 "computer will no longer be associated with it.")
                                  .arg(QtPurple::accountDisplayName(account)))
        != QMessageBox::Yes)
        return;
    if (!QtPurple::accountExists(account))
        return;
    purple_accounts_delete(account);
    scheduleRefresh();
}

/* ======================================================== AccountEditor */

AccountEditor::AccountEditor(PurpleAccount *account, QWidget *parent)
    : QDialog(parent), m_account(account)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(account ? tr("Modify Account") : tr("Add Account"));

    m_protocol = new QComboBox(this);
    QList<PurplePlugin *> prpls;
    for (GList *l = purple_plugins_get_protocols(); l; l = l->next)
        prpls.append(static_cast<PurplePlugin *>(l->data));
    std::sort(prpls.begin(), prpls.end(), [](PurplePlugin *a, PurplePlugin *b) {
        return QString::localeAwareCompare(qs(a->info->name), qs(b->info->name)) < 0;
    });
    for (PurplePlugin *p : prpls)
        m_protocol->addItem(qs(p->info->name), qs(p->info->id));

    if (account) {
        const QString id = qs(purple_account_get_protocol_id(account));
        int idx = m_protocol->findData(id);
        if (idx < 0) {
            m_protocol->addItem(tr("%1 (not available)").arg(id), id);
            idx = m_protocol->count() - 1;
        }
        m_protocol->setCurrentIndex(idx);
        m_protocol->setEnabled(false);      /* changing protocol of an existing account is unsupported */
    } else {
        const int xmpp = m_protocol->findData(QStringLiteral("prpl-jabber"));
        if (xmpp >= 0)
            m_protocol->setCurrentIndex(xmpp);
    }

    /* Basic tab */
    QWidget *basic = new QWidget(this);
    QFormLayout *form = new QFormLayout(basic);
    m_username = new QLineEdit(basic);
    m_splitBox = new QWidget(basic);
    m_splitLayout = new QFormLayout(m_splitBox);
    m_splitLayout->setContentsMargins(0, 0, 0, 0);
    m_password = new QLineEdit(basic);
    m_password->setEchoMode(QLineEdit::Password);
    m_passwordLabel = new QLabel(tr("&Password:"), basic);
    m_passwordLabel->setBuddy(m_password);
    m_remember = new QCheckBox(tr("Remember pass&word"), basic);
    m_alias = new QLineEdit(basic);
    m_checkMail = new QCheckBox(tr("New &mail notifications"), basic);
    m_register = new QCheckBox(tr("Create this new account on the &server"), basic);

    form->addRow(tr("Pro&tocol:"), m_protocol);
    form->addRow(tr("&Username:"), m_username);
    form->addRow(m_splitBox);
    form->addRow(m_passwordLabel, m_password);
    form->addRow(QString(), m_remember);
    form->addRow(tr("&Local alias:"), m_alias);
    form->addRow(QString(), m_checkMail);
    form->addRow(QString(), m_register);

    /* Advanced tab */
    m_optionBox = new QWidget(this);
    m_optionLayout = new QFormLayout(m_optionBox);

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(basic, tr("&Basic"));
    tabs->addTab(m_optionBox, tr("Ad&vanced"));

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    if (!account)
        buttons->button(QDialogButtonBox::Save)->setText(tr("&Add"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(tabs, 1);
    lay->addWidget(buttons);

    if (account) {
        m_alias->setText(qs(purple_account_get_alias(account)));
        m_remember->setChecked(purple_account_get_remember_password(account));
        m_password->setText(qs(purple_account_get_password(account)));
        m_checkMail->setChecked(purple_account_get_check_mail(account));
    }

    connect(m_protocol, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AccountEditor::rebuildProtocolWidgets);
    rebuildProtocolWidgets();
    resize(440, sizeHint().height());
}

PurplePlugin *AccountEditor::currentPrpl() const
{
    const QString id = m_protocol->currentData().toString();
    if (id.isEmpty())
        return nullptr;
    return purple_find_prpl(Utf8(id));
}

void AccountEditor::rebuildProtocolWidgets()
{
    while (m_splitLayout->rowCount() > 0)
        m_splitLayout->removeRow(0);
    while (m_optionLayout->rowCount() > 0)
        m_optionLayout->removeRow(0);
    m_splits.clear();
    m_options.clear();

    PurplePlugin *prpl = currentPrpl();
    PurplePluginProtocolInfo *info = prpl ? PURPLE_PLUGIN_PROTOCOL_INFO(prpl) : nullptr;

    /* ---- user splits ("user@server/resource") */
    QByteArray username = m_account ? QByteArray(purple_account_get_username(m_account))
                                    : m_username->text().toUtf8();
    GList *splits = info ? info->user_splits : nullptr;
    for (GList *l = splits; l; l = l->next) {
        PurpleAccountUserSplit *split = static_cast<PurpleAccountUserSplit *>(l->data);
        QLineEdit *edit = new QLineEdit(m_splitBox);
        m_splitLayout->addRow(QtPurple::mnemonicToQt(purple_account_user_split_get_text(split)) + QLatin1Char(':'), edit);
        m_splits.append(SplitWidget{ split, edit });
    }

    /* Peel split values off the end of the username, last split first. */
    for (int i = m_splits.size() - 1; i >= 0; --i) {
        PurpleAccountUserSplit *split = m_splits[i].split;
        const char sep = purple_account_user_split_get_separator(split);
        QString value;
        bool found = false;
        if (m_account) {
            const int pos = purple_account_user_split_get_reverse(split)
                ? username.lastIndexOf(sep) : username.indexOf(sep);
            if (pos >= 0) {
                value = QString::fromUtf8(username.mid(pos + 1));
                username.truncate(pos);
                found = true;
            }
        }
        if (!found)
            value = qs(purple_account_user_split_get_default_value(split));
        m_splits[i].edit->setText(value);
    }
    if (m_account || m_username->text().isEmpty())
        m_username->setText(QString::fromUtf8(username));
    m_splitBox->setVisible(!m_splits.isEmpty());

    /* ---- password / mail / register */
    const bool noPassword = info && (info->options & OPT_PROTO_NO_PASSWORD);
    m_password->setVisible(!noPassword);
    m_passwordLabel->setVisible(!noPassword);
    m_remember->setVisible(!noPassword);
    m_password->setPlaceholderText(info && (info->options & OPT_PROTO_PASSWORD_OPTIONAL)
                                       ? tr("(optional)") : QString());
    m_checkMail->setVisible(info && (info->options & OPT_PROTO_MAIL_CHECK));
    const bool canRegister = !m_account && info && info->register_user;
    m_register->setVisible(canRegister);
    m_register->setEnabled(canRegister);
    if (!canRegister)
        m_register->setChecked(false);

    /* ---- protocol options */
    GList *options = info ? info->protocol_options : nullptr;
    for (GList *l = options; l; l = l->next) {
        PurpleAccountOption *opt = static_cast<PurpleAccountOption *>(l->data);
        const char *setting = purple_account_option_get_setting(opt);
        const QString label = QtPurple::mnemonicToQt(purple_account_option_get_text(opt));
        QWidget *w = nullptr;

        switch (purple_account_option_get_type(opt)) {
        case PURPLE_PREF_BOOLEAN: {
            QCheckBox *cb = new QCheckBox(label, m_optionBox);
            const gboolean def = purple_account_option_get_default_bool(opt);
            cb->setChecked(m_account ? purple_account_get_bool(m_account, setting, def) : def);
            m_optionLayout->addRow(cb);
            w = cb;
            break;
        }
        case PURPLE_PREF_INT: {
            QSpinBox *spin = new QSpinBox(m_optionBox);
            spin->setRange(INT_MIN, INT_MAX);
            const int def = purple_account_option_get_default_int(opt);
            spin->setValue(m_account ? purple_account_get_int(m_account, setting, def) : def);
            m_optionLayout->addRow(label, spin);
            w = spin;
            break;
        }
        case PURPLE_PREF_STRING: {
            QLineEdit *edit = new QLineEdit(m_optionBox);
            const char *def = purple_account_option_get_default_string(opt);
            edit->setText(qs(m_account ? purple_account_get_string(m_account, setting, def) : def));
            if (purple_account_option_get_masked(opt))
                edit->setEchoMode(QLineEdit::Password);
            m_optionLayout->addRow(label, edit);
            w = edit;
            break;
        }
        case PURPLE_PREF_STRING_LIST: {
            QComboBox *combo = new QComboBox(m_optionBox);
            const char *def = purple_account_option_get_default_list_value(opt);
            const QString current = qs(m_account ? purple_account_get_string(m_account, setting, def) : def);
            for (GList *i = purple_account_option_get_list(opt); i; i = i->next) {
                PurpleKeyValuePair *kvp = static_cast<PurpleKeyValuePair *>(i->data);
                combo->addItem(qs(kvp->key), qs(static_cast<const char *>(kvp->value)));
            }
            const int idx = combo->findData(current);
            if (idx >= 0)
                combo->setCurrentIndex(idx);
            m_optionLayout->addRow(label, combo);
            w = combo;
            break;
        }
        default:
            continue;
        }
        m_options.append(OptionWidget{ opt, w });
    }
    if (m_options.isEmpty())
        m_optionLayout->addRow(new QLabel(tr("This protocol has no additional options."), m_optionBox));
}

QString AccountEditor::composeUsername() const
{
    QString username = m_username->text().trimmed();
    for (const SplitWidget &s : m_splits) {
        QString value = s.edit->text().trimmed();
        if (value.isEmpty())
            value = qs(purple_account_user_split_get_default_value(s.split));
        username += QChar::fromLatin1(purple_account_user_split_get_separator(s.split));
        username += value;
    }
    return username;
}

void AccountEditor::accept()
{
    if (m_account && !QtPurple::accountExists(m_account)) {
        QDialog::reject();
        return;
    }

    PurplePlugin *prpl = currentPrpl();
    if (!prpl) {
        QMessageBox::warning(this, windowTitle(), tr("The protocol plugin for this account is not available."));
        return;
    }
    PurplePluginProtocolInfo *info = PURPLE_PLUGIN_PROTOCOL_INFO(prpl);

    if (m_username->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Please enter a username."));
        return;
    }

    const QString full = composeUsername();
    Utf8 username(full);
    const char *protoId = prpl->info->id;

    PurpleAccount *existing = purple_accounts_find(username, protoId);
    if (existing && existing != m_account) {
        QMessageBox::warning(this, windowTitle(), tr("An account named %1 already exists for this protocol.").arg(full));
        return;
    }

    const bool isNew = (m_account == nullptr);
    PurpleAccount *account = isNew ? purple_account_new(username, protoId) : m_account;

    if (!isNew && full != qs(purple_account_get_username(account)))
        purple_account_set_username(account, username);

    purple_account_set_alias(account, Utf8(m_alias->text().trimmed()).orNull());

    if (!(info->options & OPT_PROTO_NO_PASSWORD)) {
        purple_account_set_remember_password(account, m_remember->isChecked() ? TRUE : FALSE);
        purple_account_set_password(account, Utf8(m_password->text()).orNull());
    }
    if (info->options & OPT_PROTO_MAIL_CHECK)
        purple_account_set_check_mail(account, m_checkMail->isChecked() ? TRUE : FALSE);

    for (const OptionWidget &o : m_options) {
        const char *setting = purple_account_option_get_setting(o.option);
        switch (purple_account_option_get_type(o.option)) {
        case PURPLE_PREF_BOOLEAN:
            purple_account_set_bool(account, setting,
                                    static_cast<QCheckBox *>(o.widget)->isChecked() ? TRUE : FALSE);
            break;
        case PURPLE_PREF_INT:
            purple_account_set_int(account, setting, static_cast<QSpinBox *>(o.widget)->value());
            break;
        case PURPLE_PREF_STRING:
            purple_account_set_string(account, setting,
                                      Utf8(static_cast<QLineEdit *>(o.widget)->text()));
            break;
        case PURPLE_PREF_STRING_LIST:
            purple_account_set_string(account, setting,
                                      Utf8(static_cast<QComboBox *>(o.widget)->currentData().toString()));
            break;
        default:
            break;
        }
    }

    if (isNew) {
        purple_accounts_add(account);
        if (m_register->isEnabled() && m_register->isChecked()) {
            purple_account_register(account);
        } else {
            PurpleSavedStatus *saved = purple_savedstatus_get_current();
            if (saved) {
                purple_savedstatus_activate_for_account(saved, account);
                purple_account_set_enabled(account, QTPURPLE_UI_ID, TRUE);
            }
        }
    }

    QDialog::accept();
}
