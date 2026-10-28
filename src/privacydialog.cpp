/*
 * QtPurple - a Qt front end for libpurple
 *
 * Privacy settings and allow/block lists.
 */
#include "privacydialog.h"

#include "blistdialogs.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>
#include <initializer_list>

namespace {

QPointer<PrivacyDialog> g_dialog;

void changed(PurpleAccount *account, const char *)
{
    if (g_dialog)
        g_dialog->scheduleRefresh(account);
}

const struct { PurplePrivacyType type; const char *label; } kModes[] = {
    { PURPLE_PRIVACY_ALLOW_ALL,       QT_TRANSLATE_NOOP("PrivacyDialog", "Allow all users to contact me") },
    { PURPLE_PRIVACY_ALLOW_BUDDYLIST, QT_TRANSLATE_NOOP("PrivacyDialog", "Allow only the users on my buddy list") },
    { PURPLE_PRIVACY_ALLOW_USERS,     QT_TRANSLATE_NOOP("PrivacyDialog", "Allow only the users below (allow list)") },
    { PURPLE_PRIVACY_DENY_USERS,      QT_TRANSLATE_NOOP("PrivacyDialog", "Block only the users below (block list)") },
    { PURPLE_PRIVACY_DENY_ALL,        QT_TRANSLATE_NOOP("PrivacyDialog", "Block all users") },
};

/* Send the new mode to the server, as Pidgin does. */
void applyMode(PurpleAccount *account, PurplePrivacyType type)
{
    purple_account_set_privacy_type(account, type);
    if (PurpleConnection *gc = purple_account_get_connection(account)) {
        if (purple_account_is_connected(account))
            serv_set_permit_deny(gc);
    }
}

} // namespace

/* ================================================================ Privacy */

namespace Privacy {

PurplePrivacyUiOps *uiOps()
{
    static PurplePrivacyUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.permit_added = changed;
        ops.permit_removed = changed;
        ops.deny_added = changed;
        ops.deny_removed = changed;
        ready = true;
    }
    return &ops;
}

bool isBlocked(PurpleAccount *account, const QString &who)
{
    if (!account || who.isEmpty())
        return false;
    return !purple_privacy_check(account, Utf8(who));
}

void setBlocked(PurpleAccount *account, const QString &who, bool block)
{
    if (!QtPurple::accountExists(account) || who.isEmpty())
        return;
    Utf8 name(who);
    /* libpurple adjusts the lists (and, if needed, the mode) so that
     * exactly this user changes. */
    if (block)
        purple_privacy_deny(account, name, FALSE, FALSE);
    else
        purple_privacy_allow(account, name, FALSE, FALSE);
    if (g_dialog)
        g_dialog->scheduleRefresh(account);
}

} // namespace Privacy

/* ========================================================== PrivacyDialog */

void PrivacyDialog::showDialog(PurpleAccount *account)
{
    if (!g_dialog)
        g_dialog = new PrivacyDialog();
    if (account)
        g_dialog->m_account->setAccount(account);
    g_dialog->refresh();
    g_dialog->show();
    g_dialog->raise();
    g_dialog->activateWindow();
}

PrivacyDialog::PrivacyDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Privacy"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 440);

    m_account = new AccountCombo(false, this);
    connect(m_account, &AccountCombo::accountChanged, this, [this](PurpleAccount *) { refresh(); });

    m_mode = new QComboBox(this);
    for (const auto &m : kModes)
        m_mode->addItem(tr(m.label), int(m.type));
    connect(m_mode, QOverload<int>::of(&QComboBox::activated), this, &PrivacyDialog::onModeChosen);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);

    QFormLayout *top = new QFormLayout;
    top->addRow(tr("A&ccount:"), m_account);
    top->addRow(tr("&Who can contact me:"), m_mode);

    auto makeList = [this](const QString &title, QListWidget **list, QPushButton **add,
                           QPushButton **remove) {
        QGroupBox *box = new QGroupBox(title, this);
        *list = new QListWidget(box);
        (*list)->setSelectionMode(QAbstractItemView::ExtendedSelection);
        (*list)->setSortingEnabled(true);
        *add = new QPushButton(tr("Add..."), box);
        *remove = new QPushButton(tr("Remove"), box);
        QHBoxLayout *b = new QHBoxLayout;
        b->addWidget(*add);
        b->addWidget(*remove);
        b->addStretch(1);
        QVBoxLayout *l = new QVBoxLayout(box);
        l->addWidget(*list, 1);
        l->addLayout(b);
        return box;
    };
    QGroupBox *allowBox = makeList(tr("Allow list"), &m_allow, &m_allowAdd, &m_allowRemove);
    QGroupBox *blockBox = makeList(tr("Block list"), &m_block, &m_blockAdd, &m_blockRemove);
    connect(m_allowAdd, &QPushButton::clicked, this, &PrivacyDialog::addAllow);
    connect(m_allowRemove, &QPushButton::clicked, this, &PrivacyDialog::removeAllow);
    connect(m_blockAdd, &QPushButton::clicked, this, &PrivacyDialog::addBlock);
    connect(m_blockRemove, &QPushButton::clicked, this, &PrivacyDialog::removeBlock);
    connect(m_allow, &QListWidget::itemSelectionChanged, this, [this]() {
        m_allowRemove->setEnabled(!m_allow->selectedItems().isEmpty());
    });
    connect(m_block, &QListWidget::itemSelectionChanged, this, [this]() {
        m_blockRemove->setEnabled(!m_block->selectedItems().isEmpty());
    });

    QHBoxLayout *lists = new QHBoxLayout;
    lists->addWidget(allowBox);
    lists->addWidget(blockBox);

    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addWidget(m_note);
    lay->addLayout(lists, 1);
    lay->addLayout(bottom);

    refresh();
}

PurpleAccount *PrivacyDialog::account() const
{
    return m_account->account();
}

void PrivacyDialog::scheduleRefresh(PurpleAccount *acct)
{
    if (acct && acct != account())
        return;
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, &PrivacyDialog::refresh);
}

void PrivacyDialog::refresh()
{
    m_refreshPending = false;
    PurpleAccount *acct = account();
    m_filling = true;
    m_allow->clear();
    m_block->clear();
    const bool has = acct != nullptr;
    for (QWidget *w : std::initializer_list<QWidget *>{ m_mode, m_allow, m_block, m_allowAdd, m_blockAdd })
        w->setEnabled(has);
    m_allowRemove->setEnabled(false);
    m_blockRemove->setEnabled(false);
    if (!acct) {
        m_note->setText(tr("There are no accounts."));
        m_filling = false;
        return;
    }

    for (GSList *l = acct->permit; l; l = l->next)
        m_allow->addItem(qs(static_cast<const char *>(l->data)));
    for (GSList *l = acct->deny; l; l = l->next)
        m_block->addItem(qs(static_cast<const char *>(l->data)));

    const int idx = m_mode->findData(int(purple_account_get_privacy_type(acct)));
    if (idx >= 0)
        m_mode->setCurrentIndex(idx);

    QString note;
    if (!purple_account_is_connected(acct))
        note = tr("The account is offline: changes are stored now and sent to the server at the next sign-on, "
                  "if the protocol keeps privacy lists on the server.");
    m_note->setText(note);
    m_note->setVisible(!note.isEmpty());
    m_filling = false;
}

void PrivacyDialog::onModeChosen(int)
{
    PurpleAccount *acct = account();
    if (!acct || m_filling)
        return;
    applyMode(acct, static_cast<PurplePrivacyType>(m_mode->currentData().toInt()));
    scheduleRefresh(acct);
}

void PrivacyDialog::addTo(bool allowList)
{
    PurpleAccount *acct = account();
    if (!acct)
        return;
    bool ok = false;
    const QString who = QInputDialog::getText(this, allowList ? tr("Allow User") : tr("Block User"),
                                              tr("Username:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || who.isEmpty() || !QtPurple::accountExists(acct))
        return;
    if (allowList)
        purple_privacy_permit_add(acct, Utf8(who), FALSE);
    else
        purple_privacy_deny_add(acct, Utf8(who), FALSE);
    scheduleRefresh(acct);
}

void PrivacyDialog::removeFrom(bool allowList)
{
    PurpleAccount *acct = account();
    if (!acct)
        return;
    QListWidget *list = allowList ? m_allow : m_block;
    QStringList names;
    for (QListWidgetItem *it : list->selectedItems())
        names << it->text();
    for (const QString &who : names) {
        if (!QtPurple::accountExists(acct))
            break;
        if (allowList)
            purple_privacy_permit_remove(acct, Utf8(who), FALSE);
        else
            purple_privacy_deny_remove(acct, Utf8(who), FALSE);
    }
    scheduleRefresh(acct);
}

void PrivacyDialog::addAllow()
{
    addTo(true);
}

void PrivacyDialog::removeAllow()
{
    removeFrom(true);
}

void PrivacyDialog::addBlock()
{
    addTo(false);
}

void PrivacyDialog::removeBlock()
{
    removeFrom(false);
}
