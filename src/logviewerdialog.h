/*
 * QtPurple - a Qt front end for libpurple
 *
 * Conversation log viewer. libpurple itself writes the logs (see
 * Preferences -> Logging); this window reads them back with
 * purple_log_get_logs() / purple_log_read().
 *
 * Entry points, one per kind of conversation:
 *   - an open conversation tab   (Conversation -> View Log, Ctrl+Shift+L)
 *   - a buddy on the buddy list  (context menu -> View Log)
 *   - a chat on the buddy list   (context menu -> View Log)
 *
 * One viewer window exists per conversation: asking again raises it.
 */
#ifndef QTPURPLE_LOGVIEWERDIALOG_H
#define QTPURPLE_LOGVIEWERDIALOG_H

#include "common.h"

#include <QByteArray>
#include <QDialog>
#include <QList>

class HistoryView;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace LogViewer {

/* Where logs are looked up: libpurple names a log by type, account and
 * conversation name. A contact with several buddies has several sources. */
struct Source {
    PurpleLogType type;
    QByteArray name;
    PurpleAccount *account;
};

/* An IM or chat conversation that is (or was) open. */
void showForConversation(PurpleConversation *conv);

/* All IM logs of the buddy's contact. */
void showForBuddy(PurpleBuddy *buddy);

/* The logs of a chat entry on the buddy list. */
void showForChat(PurpleChat *chat);

} // namespace LogViewer

class LogViewerDialog : public QDialog
{
    Q_OBJECT
public:
    LogViewerDialog(const QString &title, const QList<LogViewer::Source> &sources,
                    QWidget *parent = nullptr);
    ~LogViewerDialog() override;

private Q_SLOTS:
    void load();
    void onSelectionChanged();

private:
    struct Entry {
        PurpleLog *log;
        QString who;            /* buddy name; only set when several buddies are merged */
    };

    void freeLogs();

    QList<LogViewer::Source> m_sources;
    QList<Entry> m_logs;         /* newest first; owned, freed with purple_log_free() */
    QLabel *m_summary;
    QLabel *m_note;
    QTreeWidget *m_list;
    HistoryView *m_view;
    QPushButton *m_reload;
};

#endif
