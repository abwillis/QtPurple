/*
 * QtPurple - a Qt front end for libpurple
 *
 * Buddy pounces.
 */
#include "pounces.h"

#include "blistdialogs.h"
#include "core.h"
#include "sound.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

/* Action names and attributes shared with Pidgin. */
#define ACTION_OPEN_WINDOW   "open-window"
#define ACTION_POPUP         "popup-notify"
#define ACTION_SEND_MESSAGE  "send-message"
#define ACTION_EXECUTE       "execute-command"
#define ACTION_PLAY_SOUND    "play-sound"

namespace {

QPointer<PouncesDialog> g_manager;
QHash<PurplePounce *, QPointer<PounceEditor>> g_editors;
QPointer<PounceEditor> g_newEditor;

const struct {
    PurplePounceEvent event;
    const char *label;
} kEventLabels[] = {
    { PURPLE_POUNCE_SIGNON,           QT_TRANSLATE_NOOP("Pounces", "Si&gns on") },
    { PURPLE_POUNCE_SIGNOFF,          QT_TRANSLATE_NOOP("Pounces", "Signs o&ff") },
    { PURPLE_POUNCE_AWAY,             QT_TRANSLATE_NOOP("Pounces", "Goes a&way") },
    { PURPLE_POUNCE_AWAY_RETURN,      QT_TRANSLATE_NOOP("Pounces", "Ret&urns from away") },
    { PURPLE_POUNCE_IDLE,             QT_TRANSLATE_NOOP("Pounces", "Becomes i&dle") },
    { PURPLE_POUNCE_IDLE_RETURN,      QT_TRANSLATE_NOOP("Pounces", "Is no longer id&le") },
    { PURPLE_POUNCE_TYPING,           QT_TRANSLATE_NOOP("Pounces", "Starts &typing") },
    { PURPLE_POUNCE_TYPED,            QT_TRANSLATE_NOOP("Pounces", "P&auses while typing") },
    { PURPLE_POUNCE_TYPING_STOPPED,   QT_TRANSLATE_NOOP("Pounces", "Stops t&yping") },
    { PURPLE_POUNCE_MESSAGE_RECEIVED, QT_TRANSLATE_NOOP("Pounces", "Sends a &message") },
};

QString trp(const char *text)
{
    return QCoreApplication::translate("Pounces", text);
}

QString eventSentence(PurplePounceEvent event, const QString &who)
{
    switch (event) {
    case PURPLE_POUNCE_SIGNON:           return trp("%1 has signed on").arg(who);
    case PURPLE_POUNCE_SIGNOFF:          return trp("%1 has signed off").arg(who);
    case PURPLE_POUNCE_AWAY:             return trp("%1 has gone away.").arg(who);
    case PURPLE_POUNCE_AWAY_RETURN:      return trp("%1 has returned from being away.").arg(who);
    case PURPLE_POUNCE_IDLE:             return trp("%1 has become idle.").arg(who);
    case PURPLE_POUNCE_IDLE_RETURN:      return trp("%1 is no longer idle.").arg(who);
    case PURPLE_POUNCE_TYPING:           return trp("%1 has started typing to you.").arg(who);
    case PURPLE_POUNCE_TYPED:            return trp("%1 has paused while typing to you.").arg(who);
    case PURPLE_POUNCE_TYPING_STOPPED:   return trp("%1 has stopped typing to you.").arg(who);
    case PURPLE_POUNCE_MESSAGE_RECEIVED: return trp("%1 has sent you a message.").arg(who);
    default:                             return trp("Unknown pounce event. Please report this!");
    }
}

QString eventsSummary(PurplePounceEvent events)
{
    QStringList parts;
    for (const auto &e : kEventLabels) {
        if (events & e.event) {
            QString label = trp(e.label);
            label.remove(QLatin1Char('&'));
            parts << label;
        }
    }
    return parts.join(QStringLiteral(", "));
}

/* ------------------------------------------------------ pounce handler */

PurpleConversation *imConversation(PurpleAccount *account, const char *who)
{
    PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who, account);
    if (!conv)
        conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, who);
    return conv;
}

void pounceCb(PurplePounce *pounce, PurplePounceEvent events, void *)
{
    PurpleAccount *account = purple_pounce_get_pouncer(pounce);
    const QByteArray pouncee(purple_pounce_get_pouncee(pounce));
    PurpleBuddy *buddy = purple_find_buddy(account, pouncee.constData());
    const QString alias = buddy ? qs(purple_buddy_get_alias(buddy)) : QString::fromUtf8(pouncee);
    const bool connected = purple_account_is_connected(account);

    if (purple_pounce_action_is_enabled(pounce, ACTION_OPEN_WINDOW) && connected) {
        if (PurpleConversation *conv = imConversation(account, pouncee.constData()))
            purple_conversation_present(conv);
    }

    if (purple_pounce_action_is_enabled(pounce, ACTION_POPUP)) {
        QString secondary = QtPurple::accountDisplayName(account);
        if (buddy && (events & PURPLE_POUNCE_AWAY)) {
            PurpleStatus *status = purple_presence_get_active_status(purple_buddy_get_presence(buddy));
            const char *msg = status ? purple_status_get_attr_string(status, "message") : nullptr;
            if (msg && *msg) {
                gchar *plain = purple_markup_strip_html(msg);
                secondary += QStringLiteral("\n") + qs(plain);
                g_free(plain);
            }
        }
        purple_notify_info(nullptr, trp("Buddy Pounce").toUtf8().constData(),
                           eventSentence(events, alias).toUtf8().constData(),
                           secondary.toUtf8().constData());
    }

    if (purple_pounce_action_is_enabled(pounce, ACTION_SEND_MESSAGE) && connected) {
        const QString message = qs(purple_pounce_action_get_attribute(pounce, ACTION_SEND_MESSAGE, "message"));
        if (!message.trimmed().isEmpty()) {
            if (PurpleConversation *conv = imConversation(account, pouncee.constData())) {
                QString html = message.toHtmlEscaped();
                html.replace(QLatin1Char('\n'), QLatin1String("<br>"));
                purple_conv_im_send(PURPLE_CONV_IM(conv), Utf8(html));
            }
        }
    }

    if (purple_pounce_action_is_enabled(pounce, ACTION_EXECUTE)) {
        const char *command = purple_pounce_action_get_attribute(pounce, ACTION_EXECUTE, "command");
        if (command && *command) {
            /* The user's own command line from the pounce editor. */
            GError *error = nullptr;
            if (!g_spawn_command_line_async(command, &error)) {
                purple_debug_error("pounces", "Could not run pounce command: %s\n",
                                   error ? error->message : "unknown error");
                if (error)
                    g_error_free(error);
            }
        }
    }

    if (purple_pounce_action_is_enabled(pounce, ACTION_PLAY_SOUND)) {
        const char *file = purple_pounce_action_get_attribute(pounce, ACTION_PLAY_SOUND, "filename");
        if (file && *file)
            purple_sound_play_file(file, nullptr);
        else
            purple_sound_play_event(PURPLE_SOUND_POUNCE_DEFAULT, nullptr);
    }
}

void newPounce(PurplePounce *pounce)
{
    purple_pounce_action_register(pounce, ACTION_OPEN_WINDOW);
    purple_pounce_action_register(pounce, ACTION_POPUP);
    purple_pounce_action_register(pounce, ACTION_SEND_MESSAGE);
    purple_pounce_action_register(pounce, ACTION_EXECUTE);
    purple_pounce_action_register(pounce, ACTION_PLAY_SOUND);

    if (g_manager)
        g_manager->scheduleRefresh();
}

/* Called while the pounce is being freed: only the pointer value may be used. */
void freePounce(PurplePounce *pounce)
{
    auto it = g_editors.find(pounce);
    if (it != g_editors.end()) {
        if (it.value()) {
            it.value()->hide();
            it.value()->deleteLater();
        }
        g_editors.erase(it);
    }
    if (g_manager && !Core::shuttingDown())
        g_manager->scheduleRefresh();
}

} // namespace

/* ============================================================ Pounces */

namespace Pounces {

void init()
{
    purple_pounces_register_handler(QTPURPLE_UI_ID, pounceCb, newPounce, freePounce);
}

bool exists(PurplePounce *pounce)
{
    return pounce && g_list_find(purple_pounces_get_all(), pounce) != nullptr;
}

void showManager()
{
    if (!g_manager)
        g_manager = new PouncesDialog();
    g_manager->scheduleRefresh();
    g_manager->show();
    g_manager->raise();
    g_manager->activateWindow();
}

void editPounce(PurplePounce *pounce, PurpleAccount *account, const QString &buddy)
{
    if (pounce && !exists(pounce))
        return;
    QPointer<PounceEditor> &slot = pounce ? g_editors[pounce] : g_newEditor;
    if (slot && !pounce) {
        /* Re-use an open "new pounce" editor only when nothing is pre-filled. */
        if (account || !buddy.isEmpty()) {
            slot->close();
            slot = nullptr;
        }
    }
    if (!slot)
        slot = new PounceEditor(pounce, account, buddy);
    slot->show();
    slot->raise();
    slot->activateWindow();
}

} // namespace Pounces

/* ======================================================== PouncesDialog */

PouncesDialog::PouncesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Buddy Pounces"));
    resize(620, 300);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({ tr("Pounce Target"), tr("Account"), tr("Events"), tr("Recurring") });
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &PouncesDialog::onSelectionChanged);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { onModify(); });

    QPushButton *add = new QPushButton(tr("&Add..."), this);
    m_modify = new QPushButton(tr("&Modify..."), this);
    m_delete = new QPushButton(tr("&Delete"), this);
    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(add, &QPushButton::clicked, this, &PouncesDialog::onAdd);
    connect(m_modify, &QPushButton::clicked, this, &PouncesDialog::onModify);
    connect(m_delete, &QPushButton::clicked, this, &PouncesDialog::onDelete);
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

    refresh();
}

void PouncesDialog::scheduleRefresh()
{
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, &PouncesDialog::refresh);
}

void PouncesDialog::refresh()
{
    m_refreshPending = false;
    PurplePounce *selected = selectedPounce();
    m_list->clear();

    GList *list = purple_pounces_get_all_for_ui(QTPURPLE_UI_ID);
    for (GList *l = list; l; l = l->next) {
        PurplePounce *p = static_cast<PurplePounce *>(l->data);
        PurpleAccount *account = purple_pounce_get_pouncer(p);
        const char *who = purple_pounce_get_pouncee(p);
        PurpleBuddy *buddy = purple_find_buddy(account, who);

        QTreeWidgetItem *item = new QTreeWidgetItem(m_list);
        item->setText(0, buddy ? QStringLiteral("%1 (%2)").arg(qs(purple_buddy_get_alias(buddy)), qs(who))
                               : qs(who));
        item->setIcon(1, QtPurple::protocolIcon(account));
        item->setText(1, QtPurple::accountDisplayName(account));
        item->setText(2, eventsSummary(purple_pounce_get_events(p)));
        item->setToolTip(2, item->text(2));
        item->setText(3, purple_pounce_get_save(p) ? tr("Yes") : tr("No"));
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(p)));
        if (p == selected)
            m_list->setCurrentItem(item);
    }
    g_list_free(list);
    for (int i = 0; i < m_list->columnCount(); ++i)
        if (i != 2)
            m_list->resizeColumnToContents(i);
    onSelectionChanged();
}

PurplePounce *PouncesDialog::selectedPounce() const
{
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item)
        return nullptr;
    PurplePounce *p = reinterpret_cast<PurplePounce *>(item->data(0, Qt::UserRole).value<quintptr>());
    return Pounces::exists(p) ? p : nullptr;
}

void PouncesDialog::onSelectionChanged()
{
    const bool has = selectedPounce() != nullptr;
    m_modify->setEnabled(has);
    m_delete->setEnabled(has);
}

void PouncesDialog::onAdd()
{
    Pounces::editPounce(nullptr);
}

void PouncesDialog::onModify()
{
    if (PurplePounce *p = selectedPounce())
        Pounces::editPounce(p);
}

void PouncesDialog::onDelete()
{
    PurplePounce *p = selectedPounce();
    if (!p)
        return;
    const QString who = qs(purple_pounce_get_pouncee(p));
    if (QMessageBox::question(this, tr("Delete Pounce"),
                              tr("Delete the buddy pounce on %1?").arg(who)) != QMessageBox::Yes)
        return;
    if (Pounces::exists(p))
        purple_pounce_destroy(p);       /* freePounce() refreshes the list */
}

/* ========================================================= PounceEditor */

PounceEditor::PounceEditor(PurplePounce *pounce, PurpleAccount *account, const QString &buddy,
                           QWidget *parent)
    : QDialog(parent), m_pounce(pounce)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(pounce ? tr("Edit Buddy Pounce") : tr("New Buddy Pounce"));

    /* Who */
    QGroupBox *whoBox = new QGroupBox(tr("Pounce on Whom"), this);
    QFormLayout *whoForm = new QFormLayout(whoBox);
    m_account = new AccountCombo(false, whoBox);
    m_buddy = new QLineEdit(whoBox);
    whoForm->addRow(tr("A&ccount:"), m_account);
    whoForm->addRow(tr("&Buddy name:"), m_buddy);

    /* When */
    QGroupBox *whenBox = new QGroupBox(tr("Pounce When Buddy..."), this);
    QGridLayout *whenGrid = new QGridLayout(whenBox);
    int idx = 0;
    for (const auto &e : kEventLabels) {
        QCheckBox *cb = new QCheckBox(trp(e.label), whenBox);
        whenGrid->addWidget(cb, idx / 2, idx % 2);
        m_events.append(EventBox{ e.event, cb });
        ++idx;
    }

    /* Do */
    QGroupBox *actionBox = new QGroupBox(tr("Action"), this);
    QGridLayout *actionGrid = new QGridLayout(actionBox);
    m_openWindow = new QCheckBox(tr("&Open an IM window"), actionBox);
    m_popup = new QCheckBox(tr("&Pop up a notification"), actionBox);
    m_sendMessage = new QCheckBox(tr("&Send a message:"), actionBox);
    m_message = new QLineEdit(actionBox);
    m_execute = new QCheckBox(tr("E&xecute a command:"), actionBox);
    m_command = new QLineEdit(actionBox);
    m_command->setPlaceholderText(tr("program and arguments"));
    m_playSound = new QCheckBox(tr("P&lay a sound:"), actionBox);
    m_soundFile = new QLineEdit(actionBox);
    m_soundFile->setPlaceholderText(tr("(default pounce sound)"));
    QPushButton *browse = new QPushButton(tr("Br&owse..."), actionBox);
    QPushButton *testSound = new QPushButton(tr("Pre&view"), actionBox);

    actionGrid->addWidget(m_openWindow, 0, 0, 1, 4);
    actionGrid->addWidget(m_popup, 1, 0, 1, 4);
    actionGrid->addWidget(m_sendMessage, 2, 0);
    actionGrid->addWidget(m_message, 2, 1, 1, 3);
    actionGrid->addWidget(m_execute, 3, 0);
    actionGrid->addWidget(m_command, 3, 1, 1, 3);
    actionGrid->addWidget(m_playSound, 4, 0);
    actionGrid->addWidget(m_soundFile, 4, 1);
    actionGrid->addWidget(browse, 4, 2);
    actionGrid->addWidget(testSound, 4, 3);
    actionGrid->setColumnStretch(1, 1);

    connect(m_sendMessage, &QCheckBox::toggled, m_message, &QWidget::setEnabled);
    connect(m_execute, &QCheckBox::toggled, m_command, &QWidget::setEnabled);
    connect(m_playSound, &QCheckBox::toggled, m_soundFile, &QWidget::setEnabled);
    connect(m_playSound, &QCheckBox::toggled, browse, &QWidget::setEnabled);
    connect(m_playSound, &QCheckBox::toggled, testSound, &QWidget::setEnabled);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, tr("Sound Selection"), m_soundFile->text(),
                                                          tr("Sounds (*.wav *.ogg *.oga *.flac *.mp3);;All files (*)"));
        if (!file.isEmpty())
            m_soundFile->setText(file);
    });
    connect(testSound, &QPushButton::clicked, this, [this]() {
        QString file = m_soundFile->text().trimmed();
        if (file.isEmpty()) {
            int n = 0;
            const Sound::EventInfo *ev = Sound::events(&n);
            for (int i = 0; i < n; ++i)
                if (ev[i].id == PURPLE_SOUND_POUNCE_DEFAULT)
                    file = Sound::effectiveFile(ev[i]);
        }
        Sound::test(file);
    });

    /* Options */
    QGroupBox *optBox = new QGroupBox(tr("Options"), this);
    QVBoxLayout *optLay = new QVBoxLayout(optBox);
    m_onlyAway = new QCheckBox(tr("Pounce only when my status is &not Available"), optBox);
    m_recurring = new QCheckBox(tr("&Recurring"), optBox);
    optLay->addWidget(m_onlyAway);
    optLay->addWidget(m_recurring);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(whoBox);
    lay->addWidget(whenBox);
    lay->addWidget(actionBox);
    lay->addWidget(optBox);
    lay->addWidget(buttons);

    /* Initial values */
    if (pounce) {
        m_account->setAccount(purple_pounce_get_pouncer(pounce));
        m_buddy->setText(qs(purple_pounce_get_pouncee(pounce)));
        const PurplePounceEvent ev = purple_pounce_get_events(pounce);
        for (const EventBox &e : m_events)
            e.box->setChecked(ev & e.event);
        m_onlyAway->setChecked(purple_pounce_get_options(pounce) & PURPLE_POUNCE_OPTION_AWAY);
        m_openWindow->setChecked(purple_pounce_action_is_enabled(pounce, ACTION_OPEN_WINDOW));
        m_popup->setChecked(purple_pounce_action_is_enabled(pounce, ACTION_POPUP));
        m_sendMessage->setChecked(purple_pounce_action_is_enabled(pounce, ACTION_SEND_MESSAGE));
        m_message->setText(qs(purple_pounce_action_get_attribute(pounce, ACTION_SEND_MESSAGE, "message")));
        m_execute->setChecked(purple_pounce_action_is_enabled(pounce, ACTION_EXECUTE));
        m_command->setText(qs(purple_pounce_action_get_attribute(pounce, ACTION_EXECUTE, "command")));
        m_playSound->setChecked(purple_pounce_action_is_enabled(pounce, ACTION_PLAY_SOUND));
        m_soundFile->setText(qs(purple_pounce_action_get_attribute(pounce, ACTION_PLAY_SOUND, "filename")));
        m_recurring->setChecked(purple_pounce_get_save(pounce));
    } else {
        m_account->setAccount(account);
        m_buddy->setText(buddy);
        /* Same defaults as Pidgin. */
        for (const EventBox &e : m_events)
            e.box->setChecked(e.event == PURPLE_POUNCE_SIGNON);
        m_openWindow->setChecked(true);
        m_popup->setChecked(true);
    }
    m_message->setEnabled(m_sendMessage->isChecked());
    m_command->setEnabled(m_execute->isChecked());
    m_soundFile->setEnabled(m_playSound->isChecked());
    browse->setEnabled(m_playSound->isChecked());
    testSound->setEnabled(m_playSound->isChecked());

    if (m_buddy->text().isEmpty())
        m_buddy->setFocus();
}

void PounceEditor::accept()
{
    if (m_pounce && !Pounces::exists(m_pounce)) {
        QDialog::reject();
        return;
    }

    PurpleAccount *account = m_account->account();
    const QString who = m_buddy->text().trimmed();
    if (!account || who.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Please choose an account and enter a buddy name."));
        return;
    }

    int events = PURPLE_POUNCE_NONE;
    for (const EventBox &e : m_events)
        if (e.box->isChecked())
            events |= e.event;
    if (events == PURPLE_POUNCE_NONE) {
        QMessageBox::warning(this, windowTitle(), tr("Please choose at least one event."));
        return;
    }
    const PurplePounceOption options = m_onlyAway->isChecked() ? PURPLE_POUNCE_OPTION_AWAY
                                                               : PURPLE_POUNCE_OPTION_NONE;
    Utf8 name(who);

    PurplePounce *p = m_pounce;
    if (!p) {
        p = purple_pounce_new(QTPURPLE_UI_ID, account, name,
                              static_cast<PurplePounceEvent>(events), options);
        if (!p) {
            QMessageBox::warning(this, windowTitle(), tr("The pounce could not be created."));
            return;
        }
    } else {
        purple_pounce_set_events(p, static_cast<PurplePounceEvent>(events));
        purple_pounce_set_options(p, options);
        purple_pounce_set_pouncer(p, account);
        purple_pounce_set_pouncee(p, name);
    }

    purple_pounce_action_set_enabled(p, ACTION_OPEN_WINDOW, m_openWindow->isChecked());
    purple_pounce_action_set_enabled(p, ACTION_POPUP, m_popup->isChecked());
    purple_pounce_action_set_enabled(p, ACTION_SEND_MESSAGE, m_sendMessage->isChecked());
    purple_pounce_action_set_enabled(p, ACTION_EXECUTE, m_execute->isChecked());
    purple_pounce_action_set_enabled(p, ACTION_PLAY_SOUND, m_playSound->isChecked());

    const QString message = m_message->text().trimmed();
    const QString command = m_command->text().trimmed();
    const QString sound = m_soundFile->text().trimmed();
    purple_pounce_action_set_attribute(p, ACTION_SEND_MESSAGE, "message",
                                       Utf8(message).orNull());
    purple_pounce_action_set_attribute(p, ACTION_EXECUTE, "command",
                                       Utf8(command).orNull());
    purple_pounce_action_set_attribute(p, ACTION_PLAY_SOUND, "filename",
                                       Utf8(sound).orNull());
    purple_pounce_set_save(p, m_recurring->isChecked() ? TRUE : FALSE);

    if (g_manager)
        g_manager->scheduleRefresh();
    QDialog::accept();
}
