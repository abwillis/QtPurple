/*
 * QtPurple - a Qt front end for libpurple
 *
 * Dialogs used by the buddy list.
 */
#include "blistdialogs.h"

#include "core.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <climits>

/* ============================================================ AccountCombo */

static void accountComboRefresh(PurpleAccount *, gpointer data)
{
    AccountCombo *combo = static_cast<AccountCombo *>(data);
    /* Defer: libpurple may still be in the middle of changing the list. */
    QTimer::singleShot(0, combo, [combo]() { combo->refill(); });
}

AccountCombo::AccountCombo(bool onlineOnly, QWidget *parent, Filter filter)
    : QComboBox(parent), m_onlineOnly(onlineOnly), m_filter(std::move(filter))
{
    void *h = purple_accounts_get_handle();
    const char *names[] = { "account-signed-on", "account-signed-off", "account-added",
                            "account-removed", "account-enabled", "account-disabled" };
    for (const char *name : names)
        purple_signal_connect(h, name, this, PURPLE_CALLBACK(accountComboRefresh), this);

    connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_filling)
            Q_EMIT accountChanged(account());
    });
    refill();
}

AccountCombo::~AccountCombo()
{
    if (!Core::shuttingDown())
        purple_signals_disconnect_by_handle(this);
}

void AccountCombo::refill()
{
    PurpleAccount *previous = account();
    m_filling = true;
    clear();
    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        PurpleAccount *account = static_cast<PurpleAccount *>(l->data);
        if (m_onlineOnly && !purple_account_is_connected(account))
            continue;
        if (m_filter && !m_filter(account))
            continue;
        addItem(QtPurple::protocolIcon(account), QtPurple::accountDisplayName(account),
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(account)));
    }
    int idx = previous ? findData(QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(previous))) : -1;
    setCurrentIndex(idx >= 0 ? idx : (count() > 0 ? 0 : -1));
    m_filling = false;

    PurpleAccount *now = account();
    if (now != previous)
        Q_EMIT accountChanged(now);
}

PurpleAccount *AccountCombo::account() const
{
    if (currentIndex() < 0)
        return nullptr;
    PurpleAccount *account = reinterpret_cast<PurpleAccount *>(currentData().value<quintptr>());
    return QtPurple::accountExists(account) ? account : nullptr;
}

void AccountCombo::setAccount(PurpleAccount *account)
{
    if (!account)
        return;
    const int idx = findData(QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(account)));
    if (idx >= 0)
        setCurrentIndex(idx);
}

/* ============================================================== GroupCombo */

GroupCombo::GroupCombo(QWidget *parent)
    : QComboBox(parent)
{
    setEditable(true);
    for (PurpleBlistNode *n = purple_blist_get_root(); n; n = purple_blist_node_get_sibling_next(n)) {
        if (PURPLE_BLIST_NODE_IS_GROUP(n))
            addItem(qs(purple_group_get_name(PURPLE_GROUP(n))));
    }
    if (count() == 0)
        addItem(tr("Buddies"));
}

QString GroupCombo::groupName() const
{
    const QString name = currentText().trimmed();
    return name.isEmpty() ? tr("Buddies") : name;
}

static PurpleGroup *findOrCreateGroup(const QString &name)
{
    Utf8 n(name);
    PurpleGroup *group = purple_find_group(n);
    if (!group) {
        group = purple_group_new(n);
        purple_blist_add_group(group, nullptr);
    }
    return group;
}

/* ========================================================== AddBuddyDialog */

AddBuddyDialog::AddBuddyDialog(PurpleAccount *account, const QString &user,
                               const QString &group, const QString &alias, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Buddy"));
    setAttribute(Qt::WA_DeleteOnClose);

    m_account = new AccountCombo(true, this);
    m_account->setAccount(account);
    m_user = new QLineEdit(user, this);
    m_alias = new QLineEdit(alias, this);
    m_alias->setPlaceholderText(tr("(optional)"));
    m_invite = new QLineEdit(this);
    m_invite->setPlaceholderText(tr("(optional)"));
    m_group = new GroupCombo(this);
    if (!group.isEmpty())
        m_group->setCurrentText(group);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("A&ccount:"), m_account);
    form->addRow(tr("Buddy's &username:"), m_user);
    form->addRow(tr("&Alias:"), m_alias);
    form->addRow(tr("&Invite message:"), m_invite);
    form->addRow(tr("Add buddy to &group:"), m_group);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Add"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(buttons);

    if (user.isEmpty())
        m_user->setFocus();
    else
        m_alias->setFocus();
}

void AddBuddyDialog::accept()
{
    PurpleAccount *account = m_account->account();
    const QString user = m_user->text().trimmed();
    if (!account || !purple_account_is_connected(account)) {
        QMessageBox::warning(this, windowTitle(), tr("Please choose a connected account."));
        return;
    }
    if (user.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Please enter the buddy's username."));
        return;
    }

    PurpleGroup *group = findOrCreateGroup(m_group->groupName());
    Utf8 name(user);
    Utf8 alias(m_alias->text().trimmed());
    Utf8 invite(m_invite->text().trimmed());

    PurpleBuddy *buddy = purple_find_buddy_in_group(account, name, group);
    if (!buddy) {
        buddy = purple_buddy_new(account, name, alias.orNull());
        purple_blist_add_buddy(buddy, nullptr, group, nullptr);
    } else if (alias.orNull()) {
        purple_blist_alias_buddy(buddy, alias);
    }
    purple_account_add_buddy_with_invite(account, buddy, invite.orNull());

    QDialog::accept();
}

/* ============================================================== ChatDialog */

static bool accountSupportsChats(PurpleAccount *account)
{
    PurpleConnection *gc = purple_account_get_connection(account);
    if (!gc)
        return false;
    PurplePlugin *prpl = purple_connection_get_prpl(gc);
    if (!prpl)
        return false;
    PurplePluginProtocolInfo *info = PURPLE_PLUGIN_PROTOCOL_INFO(prpl);
    return info && info->chat_info && info->join_chat;
}

ChatDialog::ChatDialog(Mode mode, PurpleAccount *account, const QString &group,
                       const QString &alias, const QString &name, QWidget *parent)
    : QDialog(parent), m_mode(mode), m_initialName(name)
{
    setWindowTitle(mode == AddChat ? tr("Add Chat") : tr("Join a Chat"));
    setAttribute(Qt::WA_DeleteOnClose);

    m_account = new AccountCombo(true, this, accountSupportsChats);
    m_account->setAccount(account);
    connect(m_account, &AccountCombo::accountChanged, this, &ChatDialog::rebuildFields);

    QFormLayout *top = new QFormLayout;
    top->addRow(tr("A&ccount:"), m_account);

    m_fieldsBox = new QWidget(this);
    m_fieldsLayout = new QFormLayout(m_fieldsBox);
    m_fieldsLayout->setContentsMargins(0, 0, 0, 0);

    QFormLayout *bottom = new QFormLayout;
    if (mode == AddChat) {
        m_alias = new QLineEdit(alias, this);
        m_alias->setPlaceholderText(tr("(optional)"));
        m_group = new GroupCombo(this);
        if (!group.isEmpty())
            m_group->setCurrentText(group);
        bottom->addRow(tr("A&lias:"), m_alias);
        bottom->addRow(tr("&Group:"), m_group);
    }

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(mode == AddChat ? tr("&Add") : tr("&Join"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addWidget(m_fieldsBox);
    lay->addLayout(bottom);
    lay->addWidget(buttons);

    rebuildFields();
}

void ChatDialog::rebuildFields()
{
    while (m_fieldsLayout->rowCount() > 0)
        m_fieldsLayout->removeRow(0);
    m_fields.clear();

    PurpleAccount *account = m_account->account();
    if (!account)
        return;
    PurpleConnection *gc = purple_account_get_connection(account);
    if (!gc)
        return;
    PurplePluginProtocolInfo *info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_connection_get_prpl(gc));
    if (!info || !info->chat_info)
        return;

    GHashTable *defaults = nullptr;
    if (info->chat_info_defaults) {
        Utf8 name(m_initialName);
        defaults = info->chat_info_defaults(gc, name.orNull());
    }

    GList *entries = info->chat_info(gc);
    for (GList *l = entries; l; l = l->next) {
        struct proto_chat_entry *pce = static_cast<struct proto_chat_entry *>(l->data);
        const char *def = defaults
            ? static_cast<const char *>(g_hash_table_lookup(defaults, pce->identifier))
            : nullptr;

        Field f;
        f.identifier = QByteArray(pce->identifier);
        f.label = QtPurple::stripMnemonic(pce->label);
        f.required = pce->required;
        f.isInt = pce->is_int;

        if (pce->is_int) {
            QSpinBox *spin = new QSpinBox(m_fieldsBox);
            spin->setRange(pce->min, pce->max > pce->min ? pce->max : INT_MAX);
            if (def)
                spin->setValue(QString::fromUtf8(def).toInt());
            f.widget = spin;
        } else {
            QLineEdit *edit = new QLineEdit(m_fieldsBox);
            if (def)
                edit->setText(qs(def));
            if (pce->secret)
                edit->setEchoMode(QLineEdit::Password);
            f.widget = edit;
        }

        QString label = QtPurple::mnemonicToQt(pce->label);
        if (pce->required)
            label += QLatin1Char('*');
        m_fieldsLayout->addRow(label, f.widget);
        m_fields.append(f);
    }
    g_list_free_full(entries, g_free);
    if (defaults)
        g_hash_table_destroy(defaults);

    if (!m_fields.isEmpty())
        m_fields.first().widget->setFocus();
}

GHashTable *ChatDialog::collectComponents(QString *error) const
{
    GHashTable *components = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (const Field &f : m_fields) {
        QString value;
        if (f.isInt)
            value = QString::number(static_cast<QSpinBox *>(f.widget)->value());
        else
            value = static_cast<QLineEdit *>(f.widget)->text().trimmed();

        if (value.isEmpty()) {
            if (f.required) {
                *error = tr("\"%1\" is required.").arg(f.label);
                g_hash_table_destroy(components);
                return nullptr;
            }
            continue;
        }
        g_hash_table_replace(components, g_strdup(f.identifier.constData()),
                             g_strdup(value.toUtf8().constData()));
    }
    return components;
}

void ChatDialog::accept()
{
    PurpleAccount *account = m_account->account();
    PurpleConnection *gc = account ? purple_account_get_connection(account) : nullptr;
    if (!gc) {
        QMessageBox::warning(this, windowTitle(), tr("Please choose a connected account."));
        return;
    }

    QString error;
    GHashTable *components = collectComponents(&error);
    if (!components) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }

    if (m_mode == JoinChat) {
        serv_join_chat(gc, components);
        g_hash_table_destroy(components);
    } else {
        Utf8 alias(m_alias->text().trimmed());
        PurpleChat *chat = purple_chat_new(account, alias.orNull(), components);
        if (!chat) {
            /* purple_chat_new() only fails on bad arguments and then does
             * not take ownership of the table. */
            g_hash_table_destroy(components);
            QMessageBox::warning(this, windowTitle(), tr("The chat could not be created."));
            return;
        }
        purple_blist_add_chat(chat, findOrCreateGroup(m_group->groupName()), nullptr);
    }
    QDialog::accept();
}

/* ======================================================= AccountNameDialog */

AccountNameDialog::AccountNameDialog(const QString &title, const QString &prompt,
                                     Callback callback, QWidget *parent)
    : QDialog(parent), m_callback(std::move(callback))
{
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);

    m_account = new AccountCombo(true, this);
    m_name = new QLineEdit(this);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("A&ccount:"), m_account);
    form->addRow(prompt, m_name);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(buttons);
    m_name->setFocus();
}

void AccountNameDialog::accept()
{
    PurpleAccount *account = m_account->account();
    const QString name = m_name->text().trimmed();
    if (!account || !purple_account_is_connected(account) || name.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Please choose a connected account and enter a name."));
        return;
    }
    if (m_callback)
        m_callback(account, name);
    QDialog::accept();
}
