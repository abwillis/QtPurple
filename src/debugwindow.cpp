/*
 * QtPurple - a Qt front end for libpurple
 *
 * Debug window fed by PurpleDebugUiOps.
 */
#include "debugwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QApplication>
#include <QVBoxLayout>

#include <cstring>

static QPointer<DebugWindow> s_debugWindow;

static void debugPrint(PurpleDebugLevel level, const char *category, const char *arg)
{
    /* Messages from helper threads (Win32 DNS) are not shown. */
    if (!qApp || QThread::currentThread() != qApp->thread())
        return;
    if (s_debugWindow && s_debugWindow->isVisible())
        s_debugWindow->append(level, category, arg);
}

static gboolean debugIsEnabled(PurpleDebugLevel, const char *)
{
    if (!qApp || QThread::currentThread() != qApp->thread())
        return FALSE;
    return DebugWindow::isActive() ? TRUE : FALSE;
}

PurpleDebugUiOps *DebugWindow::uiOps()
{
    static PurpleDebugUiOps ops;
    static bool ready = false;
    if (!ready) {
        memset(&ops, 0, sizeof(ops));
        ops.print = debugPrint;
        ops.is_enabled = debugIsEnabled;
        ready = true;
    }
    return &ops;
}

DebugWindow *DebugWindow::instance()
{
    if (!s_debugWindow)
        s_debugWindow = new DebugWindow();
    return s_debugWindow;
}

bool DebugWindow::isActive()
{
    return s_debugWindow && s_debugWindow->isVisible();
}

DebugWindow::DebugWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Debug Window"));
    resize(720, 420);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setMaximumBlockCount(5000);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_text->setFont(mono);

    m_pause = new QCheckBox(tr("&Pause"), this);

    m_level = new QComboBox(this);
    m_level->addItem(tr("All"), int(PURPLE_DEBUG_ALL));
    m_level->addItem(tr("Misc"), int(PURPLE_DEBUG_MISC));
    m_level->addItem(tr("Info"), int(PURPLE_DEBUG_INFO));
    m_level->addItem(tr("Warning"), int(PURPLE_DEBUG_WARNING));
    m_level->addItem(tr("Error"), int(PURPLE_DEBUG_ERROR));
    m_level->addItem(tr("Fatal"), int(PURPLE_DEBUG_FATAL));

    QPushButton *clear = new QPushButton(tr("&Clear"), this);
    connect(clear, &QPushButton::clicked, m_text, &QPlainTextEdit::clear);

    QHBoxLayout *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Level:"), this));
    bar->addWidget(m_level);
    bar->addWidget(m_pause);
    bar->addStretch(1);
    bar->addWidget(clear);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(bar);
    lay->addWidget(m_text, 1);
}

void DebugWindow::append(PurpleDebugLevel level, const char *category, const char *text)
{
    if (m_pause->isChecked())
        return;
    if (int(level) < m_level->currentData().toInt())
        return;

    QString msg = qs(text);
    while (msg.endsWith(QLatin1Char('\n')))
        msg.chop(1);

    const QString line = QStringLiteral("(%1) %2: %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
             category ? qs(category) : QStringLiteral("-"),
             msg);
    m_text->appendPlainText(line);
}

void DebugWindow::closeEvent(QCloseEvent *event)
{
    /* Hide instead of destroying so the settings survive. */
    hide();
    event->ignore();
}
