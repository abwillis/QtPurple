/*
 * QtPurple - a Qt front end for libpurple
 *
 * Room list browser and PurpleRoomlistUiOps.
 */
#include "roomlistdialog.h"

#include "blistdialogs.h"
#include "blistwindow.h"
#include "core.h"

#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>

namespace {

QPointer<RoomListDialog> g_dialog;
RoomListDialog *g_fetching = nullptr;   /* dialog calling purple_roomlist_get_list() */

enum { RoleRoom = Qt::UserRole };

RoomListDialog *dialogFor(PurpleRoomlist *list)
{
    if (!list || !list->ui_data || !g_dialog)
        return nullptr;
    return list->ui_data == g_dialog.data() ? g_dialog.data() : nullptr;
}

bool accountHasRoomList(PurpleAccount *account)
{
    PurpleConnection *gc = purple_account_get_connection(account);
    if (!gc || !purple_account_is_connected(account))
        return false;
    PurplePlugin *prpl = purple_connection_get_prpl(gc);
    PurplePluginProtocolInfo *info = prpl ? PURPLE_PLUGIN_PROTOCOL_INFO(prpl) : nullptr;
    return info && info->roomlist_get_list;
}

/* ---------------------------------------------------------------- ui ops */

void uiShowWithAccount(PurpleAccount *account)
{
    RoomListDialog::showDialog(account, true);
}

void uiCreate(PurpleRoomlist *list)
{
    /* Called inside purple_roomlist_get_list(): tie the new list to the
     * dialog that asked for it. Lists created otherwise stay unowned. */
    if (g_fetching)
        list->ui_data = g_fetching;
}

void uiSetFields(PurpleRoomlist *list, GList *fields)
{
    if (RoomListDialog *d = dialogFor(list))
        d->setFields(fields);
}

void uiAddRoom(PurpleRoomlist *list, PurpleRoomlistRoom *room)
{
    if (RoomListDialog *d = dialogFor(list))
        d->addRoom(room);
}

void uiInProgress(PurpleRoomlist *list, gboolean flag)
{
    if (RoomListDialog *d = dialogFor(list))
        d->setInProgress(flag);
}

void uiDestroy(PurpleRoomlist *list)
{
    if (RoomListDialog *d = dialogFor(list))
        d->listDestroyed(list);
    list->ui_data = nullptr;
}

/* The list's account still exists and is signed on (cancel/join/expand
 * need its connection). */
bool listConnected(PurpleRoomlist *list)
{
    return list && QtPurple::accountExists(list->account) &&
           purple_account_is_connected(list->account);
}

QString fieldText(PurpleRoomlistField *field, gpointer value)
{
    switch (purple_roomlist_field_get_type(field)) {
    case PURPLE_ROOMLIST_FIELD_BOOL:
        return GPOINTER_TO_INT(value) ? RoomListDialog::tr("Yes") : RoomListDialog::tr("No");
    case PURPLE_ROOMLIST_FIELD_INT:
        return QString::number(GPOINTER_TO_INT(value));
    case PURPLE_ROOMLIST_FIELD_STRING: {
        /* Topics may carry markup. */
        const char *s = static_cast<const char *>(value);
        if (!s)
            return QString();
        gchar *plain = purple_markup_strip_html(s);
        const QString r = qs(plain).simplified();
        g_free(plain);
        return r;
    }
    }
    return QString();
}

/* Numeric columns sort numerically. */
class RoomItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        bool a = false, b = false;
        const qlonglong x = text(col).toLongLong(&a);
        const qlonglong y = other.text(col).toLongLong(&b);
        if (a && b)
            return x < y;
        return QString::localeAwareCompare(text(col), other.text(col)) < 0;
    }
};

} // namespace

PurpleRoomlistUiOps *RoomListDialog::uiOps()
{
    static PurpleRoomlistUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.show_with_account = uiShowWithAccount;
        ops.create = uiCreate;
        ops.set_fields = uiSetFields;
        ops.add_room = uiAddRoom;
        ops.in_progress = uiInProgress;
        ops.destroy = uiDestroy;
        ready = true;
    }
    return &ops;
}

void RoomListDialog::showDialog(PurpleAccount *account, bool fetchNow)
{
    if (!g_dialog)
        g_dialog = new RoomListDialog();
    if (account)
        g_dialog->m_account->setAccount(account);
    g_dialog->show();
    g_dialog->raise();
    g_dialog->activateWindow();
    if (fetchNow && account && g_dialog->m_account->account() == account)
        g_dialog->fetch();
}

RoomListDialog::RoomListDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Room List"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 460);

    m_account = new AccountCombo(true, this, accountHasRoomList);
    connect(m_account, &AccountCombo::accountChanged, this, &RoomListDialog::onAccountChanged);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter rooms"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &RoomListDialog::applyFilter);

    QFormLayout *top = new QFormLayout;
    top->addRow(tr("A&ccount:"), m_account);
    top->addRow(tr("&Filter:"), m_filter);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderLabels({ tr("Name") });
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(false);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &RoomListDialog::onSelectionChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &RoomListDialog::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *, int) { join(); });

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setMaximumHeight(fontMetrics().height());
    m_progress->hide();
    m_count = new QLabel(this);

    m_fetch = new QPushButton(tr("&Get List"), this);
    m_stop = new QPushButton(tr("&Stop"), this);
    m_join = new QPushButton(tr("&Join"), this);
    m_add = new QPushButton(tr("&Add Chat..."), this);
    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(m_fetch, &QPushButton::clicked, this, &RoomListDialog::fetch);
    connect(m_stop, &QPushButton::clicked, this, &RoomListDialog::stop);
    connect(m_join, &QPushButton::clicked, this, &RoomListDialog::join);
    connect(m_add, &QPushButton::clicked, this, &RoomListDialog::addChat);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    QHBoxLayout *status = new QHBoxLayout;
    status->addWidget(m_count, 1);
    status->addWidget(m_progress);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_fetch);
    buttons->addWidget(m_stop);
    buttons->addStretch(1);
    buttons->addWidget(m_add);
    buttons->addWidget(m_join);
    buttons->addWidget(close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addWidget(m_tree, 1);
    lay->addLayout(status);
    lay->addLayout(buttons);

    setInProgress(false);
}

RoomListDialog::~RoomListDialog()
{
    releaseList();
}

void RoomListDialog::closeEvent(QCloseEvent *event)
{
    releaseList();
    QDialog::closeEvent(event);
}

bool RoomListDialog::listUsable() const
{
    return listConnected(m_list);
}

void RoomListDialog::releaseList()
{
    if (!m_list)
        return;
    PurpleRoomlist *list = m_list;
    m_list = nullptr;
    m_tree->clear();
    m_items.clear();
    if (Core::shuttingDown())
        return;             /* libpurple is going away with its lists */
    if (purple_roomlist_get_in_progress(list) && listConnected(list))
        purple_roomlist_cancel_get_list(list);
    list->ui_data = nullptr;
    purple_roomlist_unref(list);
}

void RoomListDialog::onAccountChanged(PurpleAccount *account)
{
    /* A list belongs to one account. */
    if (m_list && m_list->account != account) {
        releaseList();
        m_tree->setHeaderLabels({ tr("Name") });
        m_rooms = 0;
        m_count->clear();
    }
    setInProgress(m_list && purple_roomlist_get_in_progress(m_list));
}

void RoomListDialog::fetch()
{
    PurpleAccount *account = m_account->account();
    if (!account || !accountHasRoomList(account))
        return;
    releaseList();
    m_rooms = 0;
    m_columns.clear();
    m_tree->setColumnCount(1);
    m_tree->setHeaderLabels({ tr("Name") });
    m_tree->setSortingEnabled(false);

    PurpleConnection *gc = purple_account_get_connection(account);
    g_fetching = this;
    PurpleRoomlist *list = purple_roomlist_get_list(gc);
    g_fetching = nullptr;
    if (!list) {
        m_count->setText(tr("The room list could not be requested."));
        setInProgress(false);
        return;
    }
    /* Protocols that create the list before our create op ran. */
    list->ui_data = this;
    m_list = list;
    if (list->fields)
        setFields(list->fields);
    for (GList *l = list->rooms; l; l = l->next)
        addRoom(static_cast<PurpleRoomlistRoom *>(l->data));
    setInProgress(purple_roomlist_get_in_progress(list));
}

void RoomListDialog::stop()
{
    if (m_list && purple_roomlist_get_in_progress(m_list) && listUsable())
        purple_roomlist_cancel_get_list(m_list);
}

void RoomListDialog::setFields(GList *fields)
{
    QStringList headers{ tr("Name") };
    m_columns.clear();
    int index = 0;
    for (GList *l = fields; l; l = l->next, ++index) {
        PurpleRoomlistField *f = static_cast<PurpleRoomlistField *>(l->data);
        if (purple_roomlist_field_get_hidden(f))
            continue;
        headers << qs(purple_roomlist_field_get_label(f));
        m_columns << index;
    }
    m_tree->setColumnCount(headers.size());
    m_tree->setHeaderLabels(headers);
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);
}

void RoomListDialog::addRoom(PurpleRoomlistRoom *room)
{
    if (!m_list || m_items.contains(room))
        return;

    QTreeWidgetItem *parentItem = nullptr;
    if (PurpleRoomlistRoom *parent = purple_roomlist_room_get_parent(room))
        parentItem = m_items.value(parent, nullptr);

    RoomItem *item = parentItem ? new RoomItem(parentItem) : new RoomItem(m_tree);
    item->setText(0, qs(purple_roomlist_room_get_name(room)));
    item->setData(0, RoleRoom, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(room)));

    const bool category = purple_roomlist_room_get_type(room) & PURPLE_ROOMLIST_ROOMTYPE_CATEGORY;
    if (category) {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }

    GList *fields = m_list->fields;
    GList *values = purple_roomlist_room_get_fields(room);
    for (int col = 0; col < m_columns.size(); ++col) {
        PurpleRoomlistField *f = static_cast<PurpleRoomlistField *>(g_list_nth_data(fields, guint(m_columns[col])));
        GList *v = g_list_nth(values, guint(m_columns[col]));
        if (f && v)
            item->setText(col + 1, fieldText(f, v->data));
    }

    m_items.insert(room, item);
    if (!(purple_roomlist_room_get_type(room) & PURPLE_ROOMLIST_ROOMTYPE_CATEGORY))
        ++m_rooms;
    m_count->setText(tr("%n room(s)", nullptr, m_rooms));

    const QString filter = m_filter->text().trimmed();
    if (!filter.isEmpty())
        item->setHidden(!item->text(0).contains(filter, Qt::CaseInsensitive) &&
                        !item->text(m_tree->columnCount() - 1).contains(filter, Qt::CaseInsensitive));
}

void RoomListDialog::setInProgress(bool on)
{
    m_progress->setVisible(on);
    m_stop->setEnabled(on);
    m_fetch->setEnabled(!on && m_account->account() != nullptr);
    if (!on)
        m_tree->setSortingEnabled(true);        /* sorting during a big fetch is slow */
    onSelectionChanged();
}

void RoomListDialog::listDestroyed(PurpleRoomlist *list)
{
    if (list != m_list)
        return;
    m_tree->clear();
    m_items.clear();
    m_list = nullptr;
    setInProgress(false);
}

PurpleRoomlistRoom *RoomListDialog::selectedRoom() const
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item || !m_list)
        return nullptr;
    return reinterpret_cast<PurpleRoomlistRoom *>(item->data(0, RoleRoom).value<quintptr>());
}

void RoomListDialog::onSelectionChanged()
{
    PurpleRoomlistRoom *room = selectedRoom();
    const bool isRoom = room && (purple_roomlist_room_get_type(room) & PURPLE_ROOMLIST_ROOMTYPE_ROOM);
    m_join->setEnabled(isRoom && listUsable());
    m_add->setEnabled(isRoom && listUsable());
}

void RoomListDialog::onItemExpanded(QTreeWidgetItem *item)
{
    PurpleRoomlistRoom *room = reinterpret_cast<PurpleRoomlistRoom *>(item->data(0, RoleRoom).value<quintptr>());
    if (!room || !m_list || !listUsable())
        return;
    if (!(purple_roomlist_room_get_type(room) & PURPLE_ROOMLIST_ROOMTYPE_CATEGORY))
        return;
    if (room->expanded_once)
        return;
    room->expanded_once = TRUE;         /* field reserved for the UI, as in Pidgin */
    purple_roomlist_expand_category(m_list, room);
}

void RoomListDialog::join()
{
    PurpleRoomlistRoom *room = selectedRoom();
    if (!room || !(purple_roomlist_room_get_type(room) & PURPLE_ROOMLIST_ROOMTYPE_ROOM))
        return;
    if (!listUsable()) {
        QMessageBox::warning(this, windowTitle(), tr("The account is no longer connected."));
        return;
    }
    purple_roomlist_room_join(m_list, room);
}

void RoomListDialog::addChat()
{
    PurpleRoomlistRoom *room = selectedRoom();
    if (!room || !listUsable())
        return;
    PurpleAccount *account = m_list->account;
    PurpleConnection *gc = purple_account_get_connection(account);
    PurplePlugin *prpl = gc ? purple_connection_get_prpl(gc) : nullptr;
    PurplePluginProtocolInfo *info = prpl ? PURPLE_PLUGIN_PROTOCOL_INFO(prpl) : nullptr;

    QString name;
    if (info && info->roomlist_room_serialize) {
        char *s = info->roomlist_room_serialize(room);
        name = qs(s);
        g_free(s);
    } else {
        name = qs(purple_roomlist_room_get_name(room));
    }
    BuddyListWindow::instance()->showAddChatDialog(account, QString(), QString(), name);
}

void RoomListDialog::applyFilter()
{
    const QString filter = m_filter->text().trimmed();
    const int last = m_tree->columnCount() - 1;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        QTreeWidgetItem *item = it.value();
        if (filter.isEmpty() || item->childCount() > 0) {
            item->setHidden(false);
            continue;
        }
        item->setHidden(!item->text(0).contains(filter, Qt::CaseInsensitive) &&
                        !item->text(last).contains(filter, Qt::CaseInsensitive));
    }
}
