/*
 * QtPurple - a Qt front end for libpurple
 *
 * Message entry with formatting, and the formatting toolbar.
 *
 * The entry is a QTextEdit. Formatting is kept as character formats and
 * turned into the HTML subset libpurple protocols understand
 * (<b> <i> <u> <s> <font face color back size> <a href> <img id>) when
 * sending. Pasted or dropped text is inserted as plain text; pasted or
 * dropped images (clipboard image data or local image files) are inserted
 * as inline images and sent through libpurple's image store, like Pidgin.
 */
#ifndef QTPURPLE_MESSAGEINPUT_H
#define QTPURPLE_MESSAGEINPUT_H

#include "common.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QWidget>

class QAction;
class QMimeData;
class QToolBar;
class QToolButton;

/* Message entry: Enter sends, Shift+Enter inserts a newline,
 * Ctrl+Up/Down walks the sent-message history, Tab completes nicks,
 * Ctrl+B/I/U toggle bold/italic/underline. */
class InputEdit : public QTextEdit
{
    Q_OBJECT
public:
    /* An image in the entry, as it will be sent. */
    struct Image {
        QByteArray data;        /* encoded file data (PNG, JPEG, GIF, ...) */
        QString filename;       /* suggested name, e.g. "image.png" */
    };

    /* Largest image accepted (encoded size). */
    static constexpr qint64 kMaxImageBytes = 10 * 1024 * 1024;

    explicit InputEdit(QWidget *parent = nullptr);

    void addHistory();
    void setTabCompletion(bool on) { m_tabCompletion = on; }

    /* Empty the box and restore the default outgoing format. */
    void resetInput();
    /* Default outgoing format from the preferences. */
    QTextCharFormat defaultFormat() const;

    /* The message as libpurple HTML; formatting is dropped when
     * withFormatting is false. Flags are PurpleConnectionFlags.
     *
     * Images: when the connection can carry inline images and imageIds is
     * given, each image is added to libpurple's image store and written as
     * <img id="N">; the caller owns one reference per id in imageIds and
     * must release it with purple_imgstore_unref_by_id(). Images that
     * cannot be sent inline are appended to notInline (when given). */
    QString toPurpleHtml(bool withFormatting, int connectionFlags,
                         QList<int> *imageIds = nullptr,
                         QList<Image> *notInline = nullptr) const;

    /* TRUE when the entry holds at least one image. */
    bool hasImages() const;

    /* Insert images; failures are reported to the user. */
    void insertImageFiles(const QStringList &paths);
    bool insertImage(const QImage &image);

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
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    QVariant loadResource(int type, const QUrl &name) override;

private:
    void loadHistory(int pos);
    bool insertEncoded(const QByteArray &data, const QString &filename, const QImage &image);
    QStringList imageNamesInDocument() const;
    void pruneImages();

    QList<QTextDocumentFragment> m_history;
    QList<QString> m_historyKeys;
    QList<QStringList> m_historyImages;     /* image names per history entry */
    QHash<QString, Image> m_images;         /* "qtpurple-input:N" -> data */
    QHash<QString, QImage> m_imageCache;    /* decoded, for display */
    int m_imageCounter = 0;
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
    void insertImage();

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
    QToolButton *m_image;
    QToolButton *m_emoji;
    int m_flags = -1;
};

#endif
