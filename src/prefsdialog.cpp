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
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

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

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(buildConversationsPage(), tr("&Conversations"));
    tabs->addTab(buildSoundsPage(), tr("&Sounds"));
    tabs->addTab(buildIdlePage(), tr("&Away / Idle"));
    tabs->addTab(buildTrayPage(), tr("System &Tray"));

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(tabs, 1);
    lay->addWidget(bb);
    resize(560, 520);
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

    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->addWidget(idleBox);
    lay->addWidget(awayBox);
    lay->addStretch(1);
    return page;
}

/* ======================================================= conversations */

QWidget *PreferencesDialog::buildConversationsPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *box = new QGroupBox(tr("Defaults for conversations"), page);
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
                                  .arg(Emoji::userMapPath()), page);
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);

    lay->addWidget(box);
    lay->addWidget(note);
    lay->addStretch(1);
    return page;
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
