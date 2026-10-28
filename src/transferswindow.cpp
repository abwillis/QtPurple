/*
 * QtPurple - a Qt front end for libpurple
 *
 * File transfer window and PurpleXferUiOps.
 */
#include "transferswindow.h"

#include <QApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cstring>

static TransfersWindow *s_window = nullptr;

enum { ColFile = 0, ColPeer, ColProgress, ColSize, ColStatus, ColCount };
enum { RoleXfer = Qt::UserRole, RoleLocalPath, RoleFinished };

static QString sizeText(goffset size)
{
    if (size <= 0)
        return QStringLiteral("?");
    gchar *s = purple_str_size_to_units(size_t(size));
    const QString r = qs(s);
    g_free(s);
    return r;
}

static QString localPathFromGlib(const char *path)
{
#ifdef _WIN32
    return qs(path);
#else
    return path ? QFile::decodeName(path) : QString();
#endif
}

/* ================================================================ ui ops */

static void xferNew(PurpleXfer *) {}

static void xferDestroy(PurpleXfer *xfer)
{
    if (s_window)
        s_window->xferDestroyed(xfer);
    xfer->ui_data = nullptr;
}

static void xferAdd(PurpleXfer *xfer)
{
    TransfersWindow::instance()->addXfer(xfer);
}

static void xferUpdate(PurpleXfer *xfer, double)
{
    if (s_window)
        s_window->updateXfer(xfer);
}

static void xferCancel(PurpleXfer *xfer)
{
    if (s_window)
        s_window->updateXfer(xfer);
}

PurpleXferUiOps *TransfersWindow::uiOps()
{
    static PurpleXferUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.new_xfer = xferNew;
        ops.destroy = xferDestroy;
        ops.add_xfer = xferAdd;
        ops.update_progress = xferUpdate;
        ops.cancel_local = xferCancel;
        ops.cancel_remote = xferCancel;
        /* ui_write/ui_read/data_not_sent: NULL -> libpurple does the file I/O. */
        ready = true;
    }
    return &ops;
}

/* ======================================================= TransfersWindow */

TransfersWindow *TransfersWindow::instance()
{
    if (!s_window)
        s_window = new TransfersWindow();
    return s_window;
}

TransfersWindow::TransfersWindow()
    : QWidget(nullptr, Qt::Window)
{
    setWindowTitle(tr("File Transfers"));
    resize(680, 260);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({ tr("File"), tr("Peer"), tr("Progress"), tr("Size"), tr("Status") });
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColProgress, QHeaderView::Fixed);
    m_tree->header()->resizeSection(ColProgress, 140);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &TransfersWindow::onSelectionChanged);

    m_cancel = new QPushButton(tr("&Cancel Transfer"), this);
    m_open = new QPushButton(tr("&Open Folder"), this);
    QPushButton *clear = new QPushButton(tr("C&lear Finished"), this);
    QPushButton *close = new QPushButton(tr("Close"), this);
    connect(m_cancel, &QPushButton::clicked, this, &TransfersWindow::onCancel);
    connect(m_open, &QPushButton::clicked, this, &TransfersWindow::onOpenFolder);
    connect(clear, &QPushButton::clicked, this, &TransfersWindow::onClearFinished);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_open);
    buttons->addWidget(clear);
    buttons->addStretch(1);
    buttons->addWidget(close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(m_tree, 1);
    lay->addLayout(buttons);
    onSelectionChanged();
}

void TransfersWindow::present()
{
    show();
    raise();
    activateWindow();
}

void TransfersWindow::addXfer(PurpleXfer *xfer)
{
    if (m_items.contains(xfer)) {
        updateXfer(xfer);
        return;
    }
    QTreeWidgetItem *item = new QTreeWidgetItem(m_tree);
    item->setData(ColFile, RoleXfer, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(xfer)));
    QProgressBar *bar = new QProgressBar(m_tree);
    bar->setRange(0, 100);
    bar->setValue(0);
    m_tree->setItemWidget(item, ColProgress, bar);
    m_items.insert(xfer, item);
    xfer->ui_data = item;

    updateXfer(xfer);
    show();
    raise();
}

void TransfersWindow::updateXfer(PurpleXfer *xfer)
{
    QTreeWidgetItem *item = m_items.value(xfer, nullptr);
    if (!item)
        return;

    const bool send = purple_xfer_get_type(xfer) == PURPLE_XFER_SEND;
    item->setText(ColFile, qs(purple_xfer_get_filename(xfer)));
    item->setIcon(ColFile, QApplication::style()->standardIcon(send ? QStyle::SP_ArrowUp
                                                                    : QStyle::SP_ArrowDown));
    item->setText(ColPeer, qs(purple_xfer_get_remote_user(xfer)));
    item->setText(ColSize, sizeText(goffset(purple_xfer_get_size(xfer))));
    item->setData(ColFile, RoleLocalPath, localPathFromGlib(purple_xfer_get_local_filename(xfer)));

    if (QProgressBar *bar = qobject_cast<QProgressBar *>(m_tree->itemWidget(item, ColProgress)))
        bar->setValue(int(purple_xfer_get_progress(xfer) * 100.0));

    QString status;
    bool finished = false;
    switch (purple_xfer_get_status(xfer)) {
    case PURPLE_XFER_STATUS_NOT_STARTED:
        status = tr("Waiting");
        break;
    case PURPLE_XFER_STATUS_ACCEPTED:
        status = tr("Accepted");
        break;
    case PURPLE_XFER_STATUS_STARTED:
        status = tr("%1 of %2").arg(sizeText(goffset(purple_xfer_get_bytes_sent(xfer))),
                                    sizeText(goffset(purple_xfer_get_size(xfer))));
        break;
    case PURPLE_XFER_STATUS_DONE:
        status = tr("Finished");
        finished = true;
        break;
    case PURPLE_XFER_STATUS_CANCEL_LOCAL:
        status = tr("Cancelled");
        finished = true;
        break;
    case PURPLE_XFER_STATUS_CANCEL_REMOTE:
        status = tr("Cancelled by peer or failed");
        finished = true;
        break;
    default:
        status = tr("Unknown");
        break;
    }
    item->setText(ColStatus, status);
    item->setData(ColFile, RoleFinished, finished);
    onSelectionChanged();
}

void TransfersWindow::xferDestroyed(PurpleXfer *xfer)
{
    QTreeWidgetItem *item = m_items.take(xfer);
    if (!item)
        return;
    /* Keep the row for reference but drop the pointer to the freed transfer. */
    item->setData(ColFile, RoleXfer, QVariant::fromValue<quintptr>(0));
    item->setData(ColFile, RoleFinished, true);
    if (item->text(ColStatus) != tr("Finished") && !item->text(ColStatus).startsWith(tr("Cancelled")))
        item->setText(ColStatus, tr("Ended"));
    onSelectionChanged();
}

PurpleXfer *TransfersWindow::selectedXfer() const
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return nullptr;
    PurpleXfer *xfer = reinterpret_cast<PurpleXfer *>(item->data(ColFile, RoleXfer).value<quintptr>());
    return (xfer && m_items.contains(xfer)) ? xfer : nullptr;
}

void TransfersWindow::onSelectionChanged()
{
    PurpleXfer *xfer = selectedXfer();
    m_cancel->setEnabled(xfer && !purple_xfer_is_completed(xfer) && !purple_xfer_is_canceled(xfer));
    QTreeWidgetItem *item = m_tree->currentItem();
    m_open->setEnabled(item && !item->data(ColFile, RoleLocalPath).toString().isEmpty());
}

void TransfersWindow::onCancel()
{
    PurpleXfer *xfer = selectedXfer();
    if (xfer && !purple_xfer_is_completed(xfer) && !purple_xfer_is_canceled(xfer))
        purple_xfer_cancel_local(xfer);
}

void TransfersWindow::onOpenFolder()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    const QString path = item->data(ColFile, RoleLocalPath).toString();
    if (!path.isEmpty())
        QtPurple::openLink(QUrl::fromLocalFile(path));     /* reveals the folder only */
}

void TransfersWindow::onClearFinished()
{
    for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (!item->data(ColFile, RoleFinished).toBool())
            continue;
        PurpleXfer *xfer = reinterpret_cast<PurpleXfer *>(item->data(ColFile, RoleXfer).value<quintptr>());
        if (xfer) {
            m_items.remove(xfer);
            xfer->ui_data = nullptr;
        }
        delete item;
    }
    onSelectionChanged();
}
