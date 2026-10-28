/*
 * QtPurple - a Qt front end for libpurple
 *
 * Plugin manager.
 */
#include "pluginsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cstring>

static QPointer<PluginsDialog> s_dialog;

static bool pluginIsListed(PurplePlugin *plugin)
{
    if (!plugin || !plugin->info)
        return false;
    if (plugin->info->type != PURPLE_PLUGIN_STANDARD)
        return false;
    if (plugin->info->flags & PURPLE_PLUGIN_FLAG_INVISIBLE)
        return false;
    const char *ui = plugin->info->ui_requirement;
    return !ui || strcmp(ui, QTPURPLE_UI_ID) == 0;
}

static bool pluginExists(PurplePlugin *plugin)
{
    return plugin && g_list_find(purple_plugins_get_all(), plugin) != nullptr;
}

void PluginsDialog::showDialog()
{
    if (!s_dialog)
        s_dialog = new PluginsDialog();
    s_dialog->refresh();
    s_dialog->show();
    s_dialog->raise();
    s_dialog->activateWindow();
}

PluginsDialog::PluginsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Plugins"));
    resize(640, 460);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({ tr("Plugin"), tr("Summary") });
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::itemChanged, this, &PluginsDialog::onItemChanged);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &PluginsDialog::onSelectionChanged);

    m_details = new QTextBrowser(this);
    m_details->setOpenLinks(false);
    connect(m_details, &QTextBrowser::anchorClicked, this, [](const QUrl &u) { QtPurple::openLink(u); });

    QSplitter *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_list);
    split->addWidget(m_details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    m_configure = new QPushButton(tr("Con&figure Plugin"), this);
    connect(m_configure, &QPushButton::clicked, this, &PluginsDialog::onConfigure);
    QPushButton *rescan = new QPushButton(tr("&Rescan"), this);
    connect(rescan, &QPushButton::clicked, this, [this]() {
        purple_plugins_probe(G_MODULE_SUFFIX);
        refresh();
    });
    QPushButton *close = new QPushButton(tr("&Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_configure);
    buttons->addWidget(rescan);
    buttons->addStretch(1);
    buttons->addWidget(close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(split, 1);
    lay->addLayout(buttons);
}

void PluginsDialog::refresh()
{
    PurplePlugin *selected = selectedPlugin();
    m_filling = true;
    m_list->clear();

    QList<PurplePlugin *> plugins;
    for (GList *l = purple_plugins_get_all(); l; l = l->next) {
        PurplePlugin *p = static_cast<PurplePlugin *>(l->data);
        if (pluginIsListed(p))
            plugins.append(p);
    }
    std::sort(plugins.begin(), plugins.end(), [](PurplePlugin *a, PurplePlugin *b) {
        return QString::localeAwareCompare(qs(a->info->name), qs(b->info->name)) < 0;
    });

    for (PurplePlugin *p : plugins) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, purple_plugin_is_loaded(p) ? Qt::Checked : Qt::Unchecked);
        item->setText(0, qs(p->info->name));
        item->setText(1, qs(p->info->summary));
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(p)));
        if (p->error)
            item->setForeground(0, QBrush(Qt::red));
        if (p == selected)
            m_list->setCurrentItem(item);
    }
    m_filling = false;
    onSelectionChanged();
}

PurplePlugin *PluginsDialog::selectedPlugin() const
{
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item)
        return nullptr;
    PurplePlugin *p = reinterpret_cast<PurplePlugin *>(item->data(0, Qt::UserRole).value<quintptr>());
    return pluginExists(p) ? p : nullptr;
}

void PluginsDialog::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_filling || column != 0)
        return;
    PurplePlugin *p = reinterpret_cast<PurplePlugin *>(item->data(0, Qt::UserRole).value<quintptr>());
    if (!pluginExists(p))
        return;

    const bool want = item->checkState(0) == Qt::Checked;
    if (want == bool(purple_plugin_is_loaded(p)))
        return;

    bool ok;
    if (want)
        ok = purple_plugin_load(p);
    else
        ok = purple_plugin_unload(p);

    if (!ok) {
        QMessageBox::warning(this, tr("Plugins"),
                             (want ? tr("%1 could not be loaded.") : tr("%1 could not be unloaded."))
                                 .arg(qs(p->info->name))
                                 + (p->error ? QStringLiteral("\n\n") + qs(p->error) : QString()));
    }
    purple_plugins_save_loaded(QTPURPLE_PREF_LOADED);
    refresh();
}

void PluginsDialog::onSelectionChanged()
{
    PurplePlugin *p = selectedPlugin();
    if (!p) {
        m_details->clear();
        m_configure->setEnabled(false);
        return;
    }
    PurplePluginInfo *info = p->info;
    QString html = QStringLiteral("<b>%1 %2</b><br>%3")
        .arg(qs(info->name).toHtmlEscaped(), qs(info->version).toHtmlEscaped(),
             qs(info->description).toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>")));
    if (info->author)
        html += tr("<br><br><b>Written by:</b> %1").arg(qs(info->author).toHtmlEscaped());
    if (info->homepage) {
        const QString hp = qs(info->homepage).toHtmlEscaped();
        html += tr("<br><b>Web site:</b> <a href=\"%1\">%1</a>").arg(hp);
    }
    if (p->path)
        html += tr("<br><b>Filename:</b> %1").arg(qs(p->path).toHtmlEscaped());
    if (p->error)
        html += tr("<br><br><span style=\"color:#cc0000\"><b>Error:</b> %1</span>")
                    .arg(qs(p->error).toHtmlEscaped());
    m_details->setHtml(html);

    m_configure->setEnabled(purple_plugin_is_loaded(p) && info->prefs_info &&
                            info->prefs_info->get_plugin_pref_frame);
}

void PluginsDialog::onConfigure()
{
    PurplePlugin *p = selectedPlugin();
    if (!p || !purple_plugin_is_loaded(p) || !p->info->prefs_info ||
        !p->info->prefs_info->get_plugin_pref_frame)
        return;
    PurplePluginPrefFrame *frame = p->info->prefs_info->get_plugin_pref_frame(p);
    if (!frame)
        return;
    PluginPrefsDialog *dlg = new PluginPrefsDialog(p, frame, this);
    dlg->show();
}

/* ===================================================== PluginPrefsDialog */

PluginPrefsDialog::PluginPrefsDialog(PurplePlugin *plugin, PurplePluginPrefFrame *frame, QWidget *parent)
    : QDialog(parent), m_frame(frame)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("%1 Preferences").arg(qs(plugin->info->name)));

    QFormLayout *form = new QFormLayout;

    for (GList *l = purple_plugin_pref_frame_get_prefs(frame); l; l = l->next) {
        PurplePluginPref *pref = static_cast<PurplePluginPref *>(l->data);
        const char *name = purple_plugin_pref_get_name(pref);
        const QString label = QtPurple::mnemonicToQt(purple_plugin_pref_get_label(pref));

        if (!name || purple_plugin_pref_get_type(pref) == PURPLE_PLUGIN_PREF_INFO) {
            QLabel *l2 = new QLabel(qs(purple_plugin_pref_get_label(pref)), this);
            l2->setWordWrap(true);
            QFont f = l2->font();
            if (!name)
                f.setBold(true);    /* section header */
            l2->setFont(f);
            form->addRow(l2);
            continue;
        }

        const QByteArray path(name);
        const PurplePrefType ptype = purple_prefs_get_type(name);

        if (purple_plugin_pref_get_type(pref) == PURPLE_PLUGIN_PREF_CHOICE) {
            QComboBox *combo = new QComboBox(this);
            GList *c = purple_plugin_pref_get_choices(pref);
            int current = -1;
            while (c && c->next) {
                const char *clabel = static_cast<const char *>(c->data);
                gpointer value = c->next->data;
                if (ptype == PURPLE_PREF_INT) {
                    combo->addItem(qs(clabel), GPOINTER_TO_INT(value));
                    if (purple_prefs_get_int(name) == GPOINTER_TO_INT(value))
                        current = combo->count() - 1;
                } else if (ptype == PURPLE_PREF_BOOLEAN) {
                    combo->addItem(qs(clabel), bool(GPOINTER_TO_INT(value)));
                    if (bool(purple_prefs_get_bool(name)) == bool(GPOINTER_TO_INT(value)))
                        current = combo->count() - 1;
                } else {
                    const QString v = qs(static_cast<const char *>(value));
                    combo->addItem(qs(clabel), v);
                    if (qs(purple_prefs_get_string(name)) == v)
                        current = combo->count() - 1;
                }
                c = c->next->next;
            }
            if (current >= 0)
                combo->setCurrentIndex(current);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [combo, path, ptype](int) {
                const QVariant v = combo->currentData();
                if (ptype == PURPLE_PREF_INT)
                    purple_prefs_set_int(path.constData(), v.toInt());
                else if (ptype == PURPLE_PREF_BOOLEAN)
                    purple_prefs_set_bool(path.constData(), v.toBool() ? TRUE : FALSE);
                else
                    purple_prefs_set_string(path.constData(), Utf8(v.toString()));
            });
            form->addRow(label, combo);
            continue;
        }

        switch (ptype) {
        case PURPLE_PREF_BOOLEAN: {
            QCheckBox *cb = new QCheckBox(label, this);
            cb->setChecked(purple_prefs_get_bool(name));
            connect(cb, &QCheckBox::toggled, this, [path](bool on) {
                purple_prefs_set_bool(path.constData(), on ? TRUE : FALSE);
            });
            form->addRow(cb);
            break;
        }
        case PURPLE_PREF_INT: {
            int min = INT_MIN, max = INT_MAX;
            purple_plugin_pref_get_bounds(pref, &min, &max);
            if (min >= max) {
                min = INT_MIN;
                max = INT_MAX;
            }
            QSpinBox *spin = new QSpinBox(this);
            spin->setRange(min, max);
            spin->setValue(purple_prefs_get_int(name));
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [path](int v) {
                purple_prefs_set_int(path.constData(), v);
            });
            form->addRow(label, spin);
            break;
        }
        case PURPLE_PREF_STRING:
        case PURPLE_PREF_PATH: {
            const bool multiline = purple_plugin_pref_get_type(pref) == PURPLE_PLUGIN_PREF_STRING_FORMAT &&
                                   (purple_plugin_pref_get_format_type(pref) & PURPLE_STRING_FORMAT_TYPE_MULTILINE);
            const QString value = qs(ptype == PURPLE_PREF_PATH ? purple_prefs_get_path(name)
                                                               : purple_prefs_get_string(name));
            if (multiline) {
                QPlainTextEdit *e = new QPlainTextEdit(value, this);
                connect(e, &QPlainTextEdit::textChanged, this, [e, path, ptype]() {
                    if (ptype == PURPLE_PREF_PATH)
                        purple_prefs_set_path(path.constData(), Utf8(e->toPlainText()));
                    else
                        purple_prefs_set_string(path.constData(), Utf8(e->toPlainText()));
                });
                form->addRow(label, e);
            } else {
                QLineEdit *e = new QLineEdit(value, this);
                if (purple_plugin_pref_get_masked(pref))
                    e->setEchoMode(QLineEdit::Password);
                const unsigned maxLen = purple_plugin_pref_get_max_length(pref);
                if (maxLen > 0)
                    e->setMaxLength(int(maxLen));
                connect(e, &QLineEdit::textChanged, this, [path, ptype](const QString &t) {
                    if (ptype == PURPLE_PREF_PATH)
                        purple_prefs_set_path(path.constData(), Utf8(t));
                    else
                        purple_prefs_set_string(path.constData(), Utf8(t));
                });
                form->addRow(label, e);
            }
            break;
        }
        default:
            form->addRow(label, new QLabel(tr("(unsupported preference type)"), this));
            break;
        }
    }

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addStretch(1);
    lay->addWidget(bb);
}

PluginPrefsDialog::~PluginPrefsDialog()
{
    if (m_frame)
        purple_plugin_pref_frame_destroy(m_frame);
}
