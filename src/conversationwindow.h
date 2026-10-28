/*
 * QtPurple - a Qt front end for libpurple
 *
 * Tabbed conversation window and PurpleConversationUiOps.
 */
#ifndef QTPURPLE_CONVERSATIONWINDOW_H
#define QTPURPLE_CONVERSATIONWINDOW_H

#include "common.h"

#include <QHash>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTextBrowser>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QSplitter;
class QTabWidget;

/* Message history view that never loads external or local resources. */
class HistoryView : public QTextBrowser
{
    Q_OBJECT
public:
    explicit HistoryView(QWidget *parent = nullptr);
    QVariant loadResource(int type, const QUrl &name) override;
};

/* Message entry: Enter sends, Shift+Enter inserts a newline,
 * Ctrl+Up/Down walks the sent-message history, Tab completes nicks. */
class InputEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit InputEdit(QWidget *parent = nullptr);
    void addHistory(const QString &text);
    void setTabCompletion(bool on) { m_tabCompletion = on; }

Q_SIGNALS:
    void sendRequested();
    void completeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QStringList m_history;
    int m_historyPos = -1;
    QString m_draft;
    bool m_tabCompletion = false;
};

class ConversationView : public QWidget
{
    Q_OBJECT
public:
    enum Activity { NoActivity = 0, Typing, NewEvent, NewMessage, NickSaid };

    explicit ConversationView(PurpleConversation *conv, QWidget *parent = nullptr);

    PurpleConversation *conversation() const { return m_conv; }
    void detach();

    void writeMessage(const char *who, const char *alias, const char *message,
                      PurpleMessageFlags flags, time_t mtime);
    void addUsers(GList *cbuddies);
    void renameUser(const char *oldName, const char *newName, const char *newAlias);
    void removeUsers(GList *names);
    void updateUser(const char *name);
    void conversationUpdated(PurpleConvUpdateType type);
    void updateHeader();
    void clearHistory();

    QString title() const;
    QIcon icon() const;
    Activity activity() const { return m_activity; }
    void setActivity(Activity a);
    void focusInput();

Q_SIGNALS:
    void titleChanged(ConversationView *view);
    void activityChanged(ConversationView *view);

private Q_SLOTS:
    void send();
    void onTextChanged();
    void onAnchorClicked(const QUrl &url);
    void onUserActivated(QListWidgetItem *item);
    void onUserContextMenu(const QPoint &pos);
    void completeNick();

private:
    bool isChat() const;
    QString processImages(const QString &html);
    QString realName(const QString &chatName) const;
    void setUserItem(QListWidgetItem *item, const QString &name, const QString &alias, int flags);
    void updateUserCount();
    void systemMessage(const QString &text, bool error = false);

    PurpleConversation *m_conv;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_subtitle;
    QLabel *m_typing;
    HistoryView *m_view;
    InputEdit *m_input;
    QListWidget *m_users = nullptr;
    QLabel *m_userCount = nullptr;
    QHash<QString, QListWidgetItem *> m_userItems;
    Activity m_activity = NoActivity;
    int m_imageCounter = 0;
};

class ConversationWindow : public QMainWindow
{
    Q_OBJECT
public:
    static ConversationWindow *instance();
    static PurpleConversationUiOps *uiOps();
    static void init();
    static ConversationView *viewFor(PurpleConversation *conv);

    void addView(ConversationView *view);
    void removeView(ConversationView *view);
    void presentView(ConversationView *view);
    bool viewHasFocus(ConversationView *view) const;
    bool isViewVisibleToUser(ConversationView *view) const;
    void saveGeometryPrefs();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private Q_SLOTS:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);
    void onTitleChanged(ConversationView *view);
    void onActivityChanged(ConversationView *view);
    void populateConversationMenu();

private:
    ConversationWindow();
    ConversationView *currentView() const;

    QTabWidget *m_tabs;
    QMenu *m_convMenu;
};

#endif
