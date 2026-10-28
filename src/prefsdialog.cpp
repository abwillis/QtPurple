/*
 * QtPurple - a Qt front end for libpurple
 *
 * Preferences dialog.
 */
#include "prefsdialog.h"

#include "emoji.h"
#include "idle.h"
#include "sound.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QPixmap>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <initializer_list>

static QPointer<PreferencesDialog> s_dialog;

static QByteArray toGlibFileName(const QString &name)
{
#ifdef _WIN32
    return name.toUtf8();
#else
    return QFile::encodeName(name);
#endif
}

static QString fromGlibFileName(const char *name)
{
#ifdef _WIN32
    return qs(name);
#else
    return name ? QFile::decodeName(name) : QString();
#endif
}

/* A combo box bound to a string preference. */
static QComboBox *stringPrefCombo(QWidget *parent, const char *pref,
                                  const QList<QPair<QString, QString>> &items)
{
    QComboBox *combo = new QComboBox(parent);
    const QString current = qs(purple_prefs_get_string(pref));
    for (const auto &it : items) {
        combo->addItem(it.first, it.second);
        if (it.second == current)
            combo->setCurrentIndex(combo->count() - 1);
    }
    const QByteArray path(pref);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), combo, [combo, path](int) {
        purple_prefs_set_string(path.constData(), Utf8(combo->currentData().toString()));
    });
    return combo;
}

static QComboBox *savedStatusCombo(QWidget *parent, const char *pref, const QString &defaultLabel);

static QCheckBox *boolPrefCheck(QWidget *parent, const QString &label, const char *pref)
{
    QCheckBox *cb = new QCheckBox(label, parent);
    cb->setChecked(purple_prefs_get_bool(pref));
    const QByteArray path(pref);
    QObject::connect(cb, &QCheckBox::toggled, cb, [path](bool on) {
        purple_prefs_set_bool(path.constData(), on ? TRUE : FALSE);
    });
    return cb;
}

void PreferencesDialog::showDialog()
{
    if (!s_dialog)
        s_dialog = new PreferencesDialog();
    s_dialog->show();
    s_dialog->raise();
    s_dialog->activateWindow();
}

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setAttribute(Qt::WA_DeleteOnClose);

    /* Page list on the left, page on the right. */
    QListWidget *pages = new QListWidget(this);
    QStackedWidget *stack = new QStackedWidget(this);
    const struct { QString title; QWidget *page; } entries[] = {
        { tr("Buddy List"), buildBuddyListPage() },
        { tr("Conversations"), buildConversationsPage() },
        { tr("Logging"), buildLoggingPage() },
        { tr("Status / Idle"), buildIdlePage() },
        { tr("Sounds"), buildSoundsPage() },
        { tr("Network"), buildNetworkPage() },
        { tr("Proxy"), buildProxyPage() },
        { tr("System Tray"), buildTrayPage() },
    };
    for (const auto &e : entries) {
        pages->addItem(e.title);
        QScrollArea *scroll = new QScrollArea(stack);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(e.page);
        stack->addWidget(scroll);
    }
    pages->setMaximumWidth(pages->sizeHintForColumn(0) + 2 * pages->frameWidth() + 24);
    connect(pages, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    pages->setCurrentRow(0);

    QHBoxLayout *body = new QHBoxLayout;
    body->addWidget(pages);
    body->addWidget(stack, 1);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(body, 1);
    lay->addWidget(bb);
    resize(720, 560);
}

/* ================================================================ sounds */

QWidget *PreferencesDialog::buildSoundsPage()
{
    QWidget *page = new QWidget(this);

    m_method = stringPrefCombo(page, QTPURPLE_PREF_SOUND_METHOD, {
        { tr("Automatic"), QStringLiteral("automatic") },
        { tr("Command"), QStringLiteral("command") },
        { tr("Console beep"), QStringLiteral("beep") },
        { tr("No sounds"), QStringLiteral("none") },
    });
    connect(m_method, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PreferencesDialog::updateSoundWidgets);

    m_command = new QLineEdit(qs(purple_prefs_get_string(QTPURPLE_PREF_SOUND_COMMAND)), page);
    m_command->setPlaceholderText(tr("e.g. paplay %s"));
    m_command->setToolTip(tr("%s is replaced by the sound file name. The command is run directly, not through a shell."));
    connect(m_command, &QLineEdit::textChanged, this, [](const QString &t) {
        purple_prefs_set_string(QTPURPLE_PREF_SOUND_COMMAND, Utf8(t));
    });

    QComboBox *when = new QComboBox(page);
    when->addItem(tr("Only when available"), 1);
    when->addItem(tr("Only when not available"), 2);
    when->addItem(tr("Always"), 3);
    {
        const int idx = when->findData(purple_prefs_get_int("/purple/sound/while_status"));
        if (idx >= 0)
            when->setCurrentIndex(idx);
    }
    connect(when, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [when](int) {
        purple_prefs_set_int("/purple/sound/while_status", when->currentData().toInt());
    });

    m_volume = new QSlider(Qt::Horizontal, page);
    m_volume->setRange(0, 100);
    m_volume->setValue(purple_prefs_get_int(QTPURPLE_PREF_SOUND_VOLUME));
    connect(m_volume, &QSlider::valueChanged, this, [](int v) {
        purple_prefs_set_int(QTPURPLE_PREF_SOUND_VOLUME, v);
    });

    QCheckBox *focus = boolPrefCheck(page, tr("Play sounds for the conversation that has &focus"),
                                     QTPURPLE_PREF_SOUND_FOCUS);
    QCheckBox *mute = boolPrefCheck(page, tr("&Mute all sounds"), QTPURPLE_PREF_SOUND_MUTE);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("&Method:"), m_method);
    form->addRow(tr("Sound c&ommand:"), m_command);
    form->addRow(tr("Sounds &while:"), when);
    form->addRow(tr("&Volume:"), m_volume);
    form->addRow(focus);
    form->addRow(mute);

    m_soundList = new QTreeWidget(page);
    m_soundList->setColumnCount(2);
    m_soundList->setHeaderLabels({ tr("Event"), tr("File") });
    m_soundList->setRootIsDecorated(false);
    m_soundList->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_soundList->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(m_soundList, &QTreeWidget::itemChanged, this, &PreferencesDialog::onSoundItemChanged);
    connect(m_soundList, &QTreeWidget::itemSelectionChanged, this, &PreferencesDialog::onSoundSelectionChanged);

    m_browse = new QPushButton(tr("&Browse..."), page);
    m_reset = new QPushButton(tr("&Reset"), page);
    m_test = new QPushButton(tr("&Test"), page);
    connect(m_browse, &QPushButton::clicked, this, &PreferencesDialog::onBrowseSound);
    connect(m_reset, &QPushButton::clicked, this, &PreferencesDialog::onResetSound);
    connect(m_test, &QPushButton::clicked, this, &PreferencesDialog::onTestSound);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_test);
    buttons->addWidget(m_reset);
    buttons->addWidget(m_browse);

    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->addLayout(form);
    lay->addWidget(new QLabel(tr("Sound events:"), page));
    lay->addWidget(m_soundList, 1);
    lay->addLayout(buttons);

    fillSoundList();
    updateSoundWidgets();
    return page;
}

void PreferencesDialog::fillSoundList()
{
    const int keep = selectedEvent();
    m_filling = true;
    m_soundList->clear();
    int n = 0;
    const Sound::EventInfo *ev = Sound::events(&n);
    for (int i = 0; i < n; ++i) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_soundList);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setText(0, QCoreApplication::translate("Sound", ev[i].label));
        item->setCheckState(0, purple_prefs_get_bool(Utf8(Sound::enabledPref(ev[i]))) ? Qt::Checked : Qt::Unchecked);
        const QString custom = fromGlibFileName(purple_prefs_get_path(Utf8(Sound::filePref(ev[i]))));
        if (custom.isEmpty()) {
            const QString def = Sound::defaultFile(ev[i]);
            item->setText(1, def.isEmpty() ? tr("(default, not installed)") : tr("(default)"));
            item->setToolTip(1, def);
        } else {
            item->setText(1, QFileInfo(custom).fileName());
            item->setToolTip(1, custom);
        }
        item->setData(0, Qt::UserRole, i);
        if (i == keep)
            m_soundList->setCurrentItem(item);
    }
    m_filling = false;
    onSoundSelectionChanged();
}

int PreferencesDialog::selectedEvent() const
{
    QTreeWidgetItem *item = m_soundList ? m_soundList->currentItem() : nullptr;
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

void PreferencesDialog::onSoundItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_filling || column != 0)
        return;
    int n = 0;
    const Sound::EventInfo *ev = Sound::events(&n);
    const int i = item->data(0, Qt::UserRole).toInt();
    if (i < 0 || i >= n)
        return;
    purple_prefs_set_bool(Utf8(Sound::enabledPref(ev[i])), item->checkState(0) == Qt::Checked ? TRUE : FALSE);
}

void PreferencesDialog::onSoundSelectionChanged()
{
    updateSoundWidgets();
}

void PreferencesDialog::updateSoundWidgets()
{
    if (!m_method || !m_command || !m_volume || !m_browse || !m_reset || !m_test)
        return;     /* still being built */
    const QString method = m_method->currentData().toString();
    const bool silent = method == QLatin1String("none");
    const bool files = method == QLatin1String("automatic") || method == QLatin1String("command");
    m_command->setEnabled(method == QLatin1String("command"));
    m_volume->setEnabled(method == QLatin1String("automatic") && Sound::haveQtMultimedia());
    m_volume->setToolTip(Sound::haveQtMultimedia()
                             ? QString()
                             : tr("Volume control needs QtPurple built with Qt Multimedia."));
    const bool has = selectedEvent() >= 0;
    m_browse->setEnabled(has && files);
    m_reset->setEnabled(has && files);
    m_test->setEnabled(has && !silent);
}

void PreferencesDialog::onBrowseSound()
{
    int n = 0;
    const Sound::EventInfo *ev = Sound::events(&n);
    const int i = selectedEvent();
    if (i < 0 || i >= n)
        return;
    const QString current = Sound::effectiveFile(ev[i]);
    const QString file = QFileDialog::getOpenFileName(this, tr("Sound Selection"), current,
                                                      tr("Sounds (*.wav *.ogg *.oga *.flac *.mp3);;All files (*)"));
    if (file.isEmpty())
        return;
    purple_prefs_set_path(Utf8(Sound::filePref(ev[i])), toGlibFileName(file).constData());
    fillSoundList();
}

void PreferencesDialog::onResetSound()
{
    int n = 0;
    const Sound::EventInfo *ev = Sound::events(&n);
    const int i = selectedEvent();
    if (i < 0 || i >= n)
        return;
    purple_prefs_set_path(Utf8(Sound::filePref(ev[i])), "");
    fillSoundList();
}

void PreferencesDialog::onTestSound()
{
    int n = 0;
    const Sound::EventInfo *ev = Sound::events(&n);
    const int i = selectedEvent();
    if (i >= 0 && i < n)
        Sound::test(Sound::effectiveFile(ev[i]));
}

/* ============================================================ away/idle */

QWidget *PreferencesDialog::buildIdlePage()
{
    QWidget *page = new QWidget(this);

    QGroupBox *idleBox = new QGroupBox(tr("Idle"), page);
    QComboBox *reporting = stringPrefCombo(idleBox, "/purple/away/idle_reporting", {
        { tr("Never"), QStringLiteral("none") },
        { tr("From last sent message"), QStringLiteral("purple") },
        { tr("Based on keyboard or mouse use"), QStringLiteral("system") },
    });
    QLabel *backend = new QLabel(tr("System idle source: %1").arg(Idle::backendName()), idleBox);
    backend->setWordWrap(true);

    QSpinBox *minutes = new QSpinBox(idleBox);
    minutes->setRange(1, 24 * 60);
    minutes->setSuffix(tr(" min"));
    minutes->setValue(purple_prefs_get_int("/purple/away/mins_before_away"));
    connect(minutes, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
        purple_prefs_set_int("/purple/away/mins_before_away", v);
    });

    QCheckBox *autoAway = boolPrefCheck(idleBox, tr("Change status to &Away when idle"),
                                        "/purple/away/away_when_idle");

    QFormLayout *idleForm = new QFormLayout(idleBox);
    idleForm->addRow(tr("&Report idle time:"), reporting);
    idleForm->addRow(QString(), backend);
    idleForm->addRow(tr("&Minutes before becoming idle:"), minutes);
    idleForm->addRow(autoAway);

    QGroupBox *awayBox = new QGroupBox(tr("Away"), page);
    QComboBox *autoReply = stringPrefCombo(awayBox, "/purple/away/auto_reply", {
        { tr("Never"), QStringLiteral("never") },
        { tr("When away"), QStringLiteral("away") },
        { tr("When both away and idle"), QStringLiteral("awayidle") },
    });
    QFormLayout *awayForm = new QFormLayout(awayBox);
    awayForm->addRow(tr("Auto-&reply:"), autoReply);
    awayForm->addRow(tr("Status when &idle:"),
                     savedStatusCombo(awayBox, "/purple/savedstatus/idleaway", tr("Default (Away)")));

    QGroupBox *startBox = new QGroupBox(tr("Status at startup"), page);
    QFormLayout *startForm = new QFormLayout(startBox);
    QCheckBox *lastStatus = boolPrefCheck(startBox, tr("Use the status from the &last exit"),
                                          "/purple/savedstatus/startup_current_status");
    QComboBox *startup = savedStatusCombo(startBox, "/purple/savedstatus/startup", tr("Default (Available)"));
    startup->setEnabled(!lastStatus->isChecked());
    connect(lastStatus, &QCheckBox::toggled, startup, [startup](bool on) { startup->setEnabled(!on); });
    startForm->addRow(lastStatus);
    startForm->addRow(tr("Status to &apply at startup:"), startup);
    QLabel *savedNote = new QLabel(tr("Only saved statuses are listed; statuses set from the buddy list "
                                      "selector are temporary."), startBox);
    savedNote->setWordWrap(true);
    startForm->addRow(savedNote);

    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->addWidget(idleBox);
    lay->addWidget(awayBox);
    lay->addWidget(startBox);
    lay->addStretch(1);
    return page;
}

/* ========================================================== buddy list */

QWidget *PreferencesDialog::buildBuddyListPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *box = new QGroupBox(tr("Buddy list"), page);
    QVBoxLayout *bl = new QVBoxLayout(box);
    bl->addWidget(boolPrefCheck(box, tr("Show &offline buddies"), QTPURPLE_PREF_OFFLINE));
    bl->addWidget(boolPrefCheck(box, tr("Show &empty groups"), QTPURPLE_PREF_EMPTY));

    lay->addWidget(box);
    lay->addStretch(1);
    return page;
}

/* ======================================================= conversations */

/* Small button showing a color; clicking picks a new one. */
static void paintColorButton(QPushButton *button, const QString &color)
{
    if (QColor::isValidColor(color)) {
        QPixmap pm(16, 16);
        pm.fill(QColor(color));
        button->setIcon(QIcon(pm));
        button->setText(color);
    } else {
        button->setIcon(QIcon());
        button->setText(QCoreApplication::translate("PreferencesDialog", "Default"));
    }
}

QWidget *PreferencesDialog::buildConversationsPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *general = new QGroupBox(tr("General"), page);
    QVBoxLayout *gl = new QVBoxLayout(general);
    gl->addWidget(boolPrefCheck(general, tr("Show the &formatting toolbar"), QTPURPLE_PREF_SHOW_TOOLBAR));
    gl->addWidget(boolPrefCheck(general, tr("Show &timestamps"), QTPURPLE_PREF_TIMESTAMPS));
    gl->addWidget(boolPrefCheck(general, tr("Send &typing notifications"),
                                "/purple/conversations/im/send_typing"));
    gl->addWidget(boolPrefCheck(general, tr("Show &nickname changes in chats"),
                                "/purple/conversations/chat/show_nick_change"));

    QGroupBox *format = new QGroupBox(tr("Default formatting of my messages"), page);
    QFormLayout *ff = new QFormLayout(format);
    QPushButton *face = new QPushButton(format);
    auto paintFace = [face]() {
        const QString f = qs(purple_prefs_get_string(QTPURPLE_PREF_FONT_FACE));
        face->setText(f.isEmpty() ? QCoreApplication::translate("PreferencesDialog", "Default") : f);
    };
    paintFace();
    connect(face, &QPushButton::clicked, this, [this, paintFace]() {
        bool ok = false;
        QFont initial(qs(purple_prefs_get_string(QTPURPLE_PREF_FONT_FACE)));
        const QFont chosen = QFontDialog::getFont(&ok, initial, this, tr("Font Face"));
        if (!ok)
            return;
        purple_prefs_set_string(QTPURPLE_PREF_FONT_FACE, Utf8(chosen.family()));
        paintFace();
    });

    QPushButton *color = new QPushButton(format);
    paintColorButton(color, qs(purple_prefs_get_string(QTPURPLE_PREF_FONT_COLOR)));
    connect(color, &QPushButton::clicked, this, [this, color]() {
        const QString cur = qs(purple_prefs_get_string(QTPURPLE_PREF_FONT_COLOR));
        const QColor c = QColorDialog::getColor(QColor::isValidColor(cur) ? QColor(cur) : QColor(Qt::black),
                                                this, tr("Text Color"));
        if (!c.isValid())
            return;
        purple_prefs_set_string(QTPURPLE_PREF_FONT_COLOR, Utf8(c.name()));
        paintColorButton(color, c.name());
    });

    QSpinBox *size = new QSpinBox(format);
    size->setRange(1, 7);
    size->setValue(qBound(1, purple_prefs_get_int(QTPURPLE_PREF_FONT_SIZE), 7));
    size->setToolTip(tr("HTML font size: 3 is normal"));
    connect(size, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
        purple_prefs_set_int(QTPURPLE_PREF_FONT_SIZE, v);
    });

    QPushButton *reset = new QPushButton(tr("&Reset"), format);
    connect(reset, &QPushButton::clicked, this, [paintFace, color, size]() {
        purple_prefs_set_string(QTPURPLE_PREF_FONT_FACE, "");
        purple_prefs_set_string(QTPURPLE_PREF_FONT_COLOR, "");
        size->setValue(3);
        paintFace();
        paintColorButton(color, QString());
    });

    ff->addRow(tr("Font:"), face);
    ff->addRow(tr("Color:"), color);
    ff->addRow(tr("Size:"), size);
    ff->addRow(QString(), reset);
    QLabel *fmtNote = new QLabel(tr("Applies to new messages. Protocols that only send plain text ignore it."),
                                 format);
    fmtNote->setWordWrap(true);
    ff->addRow(fmtNote);

    QGroupBox *box = new QGroupBox(tr("Reactions and emoji (defaults)"), page);
    QVBoxLayout *bl = new QVBoxLayout(box);
    bl->addWidget(boolPrefCheck(box, tr("Show &reactions (\"Reacted with ...\")"),
                                QTPURPLE_PREF_SHOW_REACTIONS));
    bl->addWidget(boolPrefCheck(box, tr("Show &emoji for :codes:"), QTPURPLE_PREF_EMOJI_CODES));

    QLabel *note = new QLabel(tr("Each conversation can override these from its Conversation menu; "
                                 "the choice is remembered for buddies and chats on the buddy list.\n\n"
                                 "Emoji images sent as web links are not downloaded; their text "
                                 "(usually the emoji or a :code:) is shown instead.\n\n"
                                 "Codes without a standard emoji can be defined in:\n%1\n"
                                 "one per line, for example:  rainbowsmile = \U0001F308\U0001F60A")
                                  .arg(Emoji::userMapPath()), box);
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bl->addWidget(note);

    lay->addWidget(general);
    lay->addWidget(format);
    lay->addWidget(box);
    lay->addStretch(1);
    return page;
}

/* ============================================================= logging */

QWidget *PreferencesDialog::buildLoggingPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *box = new QGroupBox(tr("Logging"), page);
    QFormLayout *form = new QFormLayout(box);

    /* Formats offered by the loggers libpurple has registered. */
    QList<QPair<QString, QString>> formats;
    GList *options = purple_log_logger_get_options();
    for (GList *l = options; l && l->next; l = l->next->next)
        formats.append(qMakePair(qs(static_cast<const char *>(l->data)),
                                 qs(static_cast<const char *>(l->next->data))));
    g_list_free(options);
    form->addRow(tr("Log &format:"), stringPrefCombo(box, "/purple/logging/format", formats));

    form->addRow(boolPrefCheck(box, tr("Log all &instant messages"), "/purple/logging/log_ims"));
    form->addRow(boolPrefCheck(box, tr("Log all c&hats"), "/purple/logging/log_chats"));
    form->addRow(boolPrefCheck(box, tr("Log all &status changes to the system log"),
                               "/purple/logging/log_system"));

    gchar *dir = g_build_filename(purple_user_dir(), "logs", NULL);
    QLabel *where = new QLabel(tr("Logs are stored in %1").arg(fromGlibFileName(dir)), page);
    g_free(dir);
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);

    lay->addWidget(box);
    lay->addWidget(where);
    lay->addStretch(1);
    return page;
}

/* ============================================================= network */

static QLineEdit *stringPrefEdit(QWidget *parent, const char *pref, bool masked = false)
{
    QLineEdit *e = new QLineEdit(qs(purple_prefs_get_string(pref)), parent);
    if (masked)
        e->setEchoMode(QLineEdit::Password);
    const QByteArray path(pref);
    QObject::connect(e, &QLineEdit::editingFinished, e, [e, path]() {
        purple_prefs_set_string(path.constData(), Utf8(e->text().trimmed()));
    });
    return e;
}

static QSpinBox *intPrefSpin(QWidget *parent, const char *pref, int min, int max)
{
    QSpinBox *s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setValue(purple_prefs_get_int(pref));
    const QByteArray path(pref);
    QObject::connect(s, QOverload<int>::of(&QSpinBox::valueChanged), s, [path](int v) {
        purple_prefs_set_int(path.constData(), v);
    });
    return s;
}

QWidget *PreferencesDialog::buildNetworkPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *ipBox = new QGroupBox(tr("IP address"), page);
    QFormLayout *ipForm = new QFormLayout(ipBox);
    QLineEdit *stun = stringPrefEdit(ipBox, "/purple/network/stun_server");
    stun->setPlaceholderText(tr("e.g. stun.example.org"));
    QCheckBox *autoIp = boolPrefCheck(ipBox, tr("Use &automatically detected IP address"),
                                      "/purple/network/auto_ip");
    QLineEdit *publicIp = stringPrefEdit(ipBox, "/purple/network/public_ip");
    publicIp->setEnabled(!autoIp->isChecked());
    connect(autoIp, &QCheckBox::toggled, publicIp, [publicIp](bool on) { publicIp->setEnabled(!on); });
    QLabel *detected = new QLabel(tr("Detected: %1").arg(qs(purple_network_get_my_ip(-1))), ipBox);
    ipForm->addRow(tr("ST&UN server:"), stun);
    ipForm->addRow(autoIp);
    ipForm->addRow(QString(), detected);
    ipForm->addRow(tr("&Public IP:"), publicIp);

    QGroupBox *portBox = new QGroupBox(tr("Ports"), page);
    QFormLayout *portForm = new QFormLayout(portBox);
    portForm->addRow(boolPrefCheck(portBox, tr("Enable automatic &router port forwarding (UPnP / NAT-PMP)"),
                                   "/purple/network/map_ports"));
    QCheckBox *range = boolPrefCheck(portBox, tr("&Manually specify the range of ports to listen on"),
                                     "/purple/network/ports_range_use");
    QSpinBox *start = intPrefSpin(portBox, "/purple/network/ports_range_start", 0, 65535);
    QSpinBox *end = intPrefSpin(portBox, "/purple/network/ports_range_end", 0, 65535);
    start->setEnabled(range->isChecked());
    end->setEnabled(range->isChecked());
    connect(range, &QCheckBox::toggled, start, &QWidget::setEnabled);
    connect(range, &QCheckBox::toggled, end, &QWidget::setEnabled);
    portForm->addRow(range);
    portForm->addRow(tr("&Start port:"), start);
    portForm->addRow(tr("&End port:"), end);

    QGroupBox *turnBox = new QGroupBox(tr("Relay server (TURN)"), page);
    QFormLayout *turnForm = new QFormLayout(turnBox);
    turnForm->addRow(tr("TU&RN server:"), stringPrefEdit(turnBox, "/purple/network/turn_server"));
    turnForm->addRow(tr("&UDP port:"), intPrefSpin(turnBox, "/purple/network/turn_port", 0, 65535));
    turnForm->addRow(tr("T&CP port:"), intPrefSpin(turnBox, "/purple/network/turn_port_tcp", 0, 65535));
    turnForm->addRow(tr("Use&rname:"), stringPrefEdit(turnBox, "/purple/network/turn_username"));
    turnForm->addRow(tr("Pass&word:"), stringPrefEdit(turnBox, "/purple/network/turn_password", true));

    lay->addWidget(ipBox);
    lay->addWidget(portBox);
    lay->addWidget(turnBox);
    lay->addStretch(1);
    return page;
}

/* =============================================================== proxy */

QWidget *PreferencesDialog::buildProxyPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *box = new QGroupBox(tr("Global proxy"), page);
    QFormLayout *form = new QFormLayout(box);
    QComboBox *type = stringPrefCombo(box, "/purple/proxy/type", {
        { tr("No proxy"), QStringLiteral("none") },
        { tr("Use environment settings"), QStringLiteral("envvar") },
        { tr("HTTP"), QStringLiteral("http") },
        { tr("SOCKS 4"), QStringLiteral("socks4") },
        { tr("SOCKS 5"), QStringLiteral("socks5") },
        { tr("Tor / privacy proxy"), QStringLiteral("tor") },
    });
    QLineEdit *host = stringPrefEdit(box, "/purple/proxy/host");
    QSpinBox *port = intPrefSpin(box, "/purple/proxy/port", 0, 65535);
    port->setSpecialValueText(tr("(default)"));
    QLineEdit *user = stringPrefEdit(box, "/purple/proxy/username");
    QLineEdit *pass = stringPrefEdit(box, "/purple/proxy/password", true);
    QCheckBox *remoteDns = boolPrefCheck(box, tr("Use remote &DNS with SOCKS 4"),
                                         "/purple/proxy/socks4_remotedns");

    form->addRow(tr("Proxy &type:"), type);
    form->addRow(tr("&Host:"), host);
    form->addRow(tr("&Port:"), port);
    form->addRow(tr("&Username:"), user);
    form->addRow(tr("Pass&word:"), pass);
    form->addRow(remoteDns);

    auto update = [type, host, port, user, pass, remoteDns]() {
        const QString t = type->currentData().toString();
        const bool manual = t != QLatin1String("none") && t != QLatin1String("envvar");
        for (QWidget *w : std::initializer_list<QWidget *>{ host, port, user, pass })
            w->setEnabled(manual);
        remoteDns->setEnabled(t == QLatin1String("socks4"));
    };
    update();
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);

    QLabel *note = new QLabel(tr("Accounts use this proxy unless the account has its own proxy settings. "
                                 "The password is stored unencrypted in prefs.xml, as libpurple does."), page);
    note->setWordWrap(true);

    lay->addWidget(box);
    lay->addWidget(note);
    lay->addStretch(1);
    return page;
}

/* ============================================================== status */

/* Combo of the saved (non-transient) statuses bound to an int preference
 * holding the status creation time; 0 means libpurple's default. */
static QComboBox *savedStatusCombo(QWidget *parent, const char *pref, const QString &defaultLabel)
{
    QComboBox *combo = new QComboBox(parent);
    combo->addItem(defaultLabel, 0);
    const int current = purple_prefs_get_int(pref);
    for (GList *l = purple_savedstatuses_get_all(); l; l = l->next) {
        PurpleSavedStatus *s = static_cast<PurpleSavedStatus *>(l->data);
        if (purple_savedstatus_is_transient(s))
            continue;
        const int created = int(purple_savedstatus_get_creation_time(s));
        combo->addItem(QtPurple::statusIcon(purple_savedstatus_get_type(s)),
                       qs(purple_savedstatus_get_title(s)), created);
        if (created == current)
            combo->setCurrentIndex(combo->count() - 1);
    }
    const QByteArray path(pref);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), combo, [combo, path](int) {
        purple_prefs_set_int(path.constData(), combo->currentData().toInt());
    });
    return combo;
}

/* ================================================================ tray */

QWidget *PreferencesDialog::buildTrayPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QCheckBox *enabled = boolPrefCheck(page, tr("Show &icon in the system tray"), QTPURPLE_PREF_TRAY_ENABLED);
    QCheckBox *blink = boolPrefCheck(page, tr("&Blink the icon on unread messages"), QTPURPLE_PREF_TRAY_BLINK);
    blink->setEnabled(enabled->isChecked());
    connect(enabled, &QCheckBox::toggled, blink, &QWidget::setEnabled);

    QLabel *note = new QLabel(tr("While the tray icon is shown, closing the buddy list hides it; "
                                 "use Quit to exit."), page);
    note->setWordWrap(true);

    lay->addWidget(enabled);
    lay->addWidget(blink);
    lay->addWidget(note);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QLabel *na = new QLabel(tr("No system tray is available in this desktop session."), page);
        na->setWordWrap(true);
        QFont f = na->font();
        f.setItalic(true);
        na->setFont(f);
        lay->addWidget(na);
        enabled->setEnabled(false);
        blink->setEnabled(false);
    }
    lay->addStretch(1);
    return page;
}
