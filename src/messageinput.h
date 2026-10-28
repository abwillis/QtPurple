/*
 * QtPurple - a Qt front end for libpurple
 *
 * Message entry with formatting, and the formatting toolbar.
 *
 * The entry is a QTextEdit. Formatting is kept as character formats and
 * turned into the HTML subset libpurple protocols understand
 * (<b> <i> <u> <s> <font face color back size> <a href>) when sending.
 * Pasted or dropped content is always inserted as plain text.
 */
#ifndef QTPURPLE_MESSAGEINPUT_H
#define QTPURPLE_MESSAGEINPUT_H

#include "common.h"

#include <QList>
#include <QString>
#include <QTextCharFormat>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QWidget>

class QAction;
class QToolBar;
class QToolButton;

/* Message entry: Enter sends, Shift+Enter inserts a newline,
 * Ctrl+Up/Down walks the sent-message history, Tab completes nicks,
 * Ctrl+B/I/U toggle bold/italic/underline. */
class InputEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit InputEdit(QWidget *parent = nullptr);

    void addHistory();
    void setTabCompletion(bool on) { m_tabCompletion = on; }

    /* Empty the box and restore the default outgoing format. */
    void resetInput();
    /* Default outgoing format from the preferences. */
    QTextCharFormat defaultFormat() const;

    /* The message as libpurple HTML; formatting is dropped when
     * withFormatting is false. Flags are PurpleConnectionFlags. */
    QString toPurpleHtml(bool withFormatting, int connectionFlags) const;

    /* HTML font size 1..7 (3 is normal) of a character format. */
    static int htmlSize(const QTextCharFormat &fmt);
    static void setHtmlSize(QTextCharFormat &fmt, int size, qreal basePointSize);
    static QString fontFace(const QTextCharFormat &fmt);
    static void setFontFace(QTextCharFormat &fmt, const QString &face);

Q_SIGNALS:
    void sendRequested();
    void completeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void loadHistory(int pos);

    QList<QTextDocumentFragment> m_history;
    QList<QString> m_historyKeys;
    int m_historyPos = -1;
    QTextDocumentFragment m_draft;
    bool m_tabCompletion = false;
};

class FormatBar : public QWidget
{
    Q_OBJECT
public:
    explicit FormatBar(InputEdit *edit, QWidget *parent = nullptr);

    /* Enable the buttons the protocol supports (PurpleConnectionFlags;
     * -1 when not connected: everything enabled). */
    void setConnectionFlags(int flags);

private Q_SLOTS:
    void syncButtons();
    void toggleBold(bool on);
    void toggleItalic(bool on);
    void toggleUnderline(bool on);
    void toggleStrike(bool on);
    void growFont();
    void shrinkFont();
    void chooseFace();
    void chooseColor();
    void chooseBackground();
    void resetFormatting();
    void insertLink();

private:
    void merge(const QTextCharFormat &fmt);
    void changeSize(int delta);
    QToolButton *makeButton(const QString &text, const QString &tip, bool checkable);

    InputEdit *m_edit;
    QToolButton *m_bold;
    QToolButton *m_italic;
    QToolButton *m_underline;
    QToolButton *m_strike;
    QToolButton *m_larger;
    QToolButton *m_smaller;
    QToolButton *m_face;
    QToolButton *m_color;
    QToolButton *m_background;
    QToolButton *m_reset;
    QToolButton *m_link;
    QToolButton *m_emoji;
    int m_flags = -1;
};

#endif
