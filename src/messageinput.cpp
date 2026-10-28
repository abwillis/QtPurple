/*
 * QtPurple - a Qt front end for libpurple
 *
 * Message entry with formatting, and the formatting toolbar.
 */
#include "messageinput.h"

#include "emoji.h"

#include <QBuffer>
#include <QColorDialog>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPair>
#include <QSet>
#include <QSignalBlocker>
#include <QSize>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QToolButton>
#include <QUrl>
#include <QWidgetAction>

#include <cstring>
#include <initializer_list>
#include <utility>

namespace {

/* Character format properties set by the toolbar. They are what is sent;
 * the matching visual properties only make the entry look right. */
enum {
    PropHtmlSize = QTextFormat::UserProperty + 1,   /* int 1..7 */
    PropFace,                                       /* QString */
};

/* Relative size of HTML font sizes 1..7 against size 3. */
const qreal kSizeFactor[8] = { 1.0, 0.7, 0.85, 1.0, 1.2, 1.5, 2.0, 2.5 };

bool allowedLinkScheme(const QUrl &url)
{
    const QString s = url.scheme().toLower();
    return s == QLatin1String("http") || s == QLatin1String("https") ||
           s == QLatin1String("ftp") || s == QLatin1String("mailto") ||
           s == QLatin1String("xmpp") || s == QLatin1String("irc") || s == QLatin1String("ircs");
}

/* Resource names of images inserted into the entry. */
const char kImageScheme[] = "qtpurple-input";

/* Largest size an inline image is shown at in the entry. */
const int kPreviewWidth = 200;
const int kPreviewHeight = 150;

/* Image formats Qt can read, as a file dialog filter. */
QString imageFileFilter()
{
    QStringList patterns;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.%1").arg(QString::fromLatin1(fmt).toLower());
    patterns.removeDuplicates();
    return QCoreApplication::translate("InputEdit", "Images (%1);;All files (*)")
        .arg(patterns.join(QLatin1Char(' ')));
}

/* Encode a pasted image: PNG, or JPEG when that is much smaller and the
 * image has no transparency (e.g. a pasted photo). */
bool encodeImage(const QImage &image, QByteArray *data, QString *filename)
{
    QByteArray png;
    {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        QImageWriter w(&buf, "png");
        if (!w.write(image))
            return false;
    }
    *data = png;
    *filename = QStringLiteral("image.png");

    if (png.size() > 512 * 1024 && !image.hasAlphaChannel()) {
        QByteArray jpg;
        QBuffer buf(&jpg);
        buf.open(QIODevice::WriteOnly);
        QImageWriter w(&buf, "jpeg");
        w.setQuality(90);
        if (w.write(image) && jpg.size() < png.size() / 2) {
            *data = jpg;
            *filename = QStringLiteral("image.jpg");
        }
    }
    return true;
}

QString escapeText(const QString &text)
{
    QString out = text.toHtmlEscaped();
    out.replace(QChar(QChar::LineSeparator), QLatin1String("<br>"));
    out.replace(QChar(QChar::ParagraphSeparator), QLatin1String("<br>"));
    out.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    /* Image placeholders are handled separately. */
    out.remove(QChar(QChar::ObjectReplacementCharacter));
    return out;
}

} // namespace

/* =============================================================== InputEdit */

InputEdit::InputEdit(QWidget *parent)
    : QTextEdit(parent)
{
    setTabChangesFocus(false);
    setAcceptRichText(false);       /* paste/drop as plain text */
    setLineWrapMode(QTextEdit::WidgetWidth);
    resetInput();
}

int InputEdit::htmlSize(const QTextCharFormat &fmt)
{
    const int s = fmt.property(PropHtmlSize).toInt();
    return (s >= 1 && s <= 7) ? s : 3;
}

void InputEdit::setHtmlSize(QTextCharFormat &fmt, int size, qreal basePointSize)
{
    size = qBound(1, size, 7);
    fmt.setProperty(PropHtmlSize, size);
    if (basePointSize > 0)
        fmt.setFontPointSize(basePointSize * kSizeFactor[size]);
}

QString InputEdit::fontFace(const QTextCharFormat &fmt)
{
    return fmt.property(PropFace).toString();
}

void InputEdit::setFontFace(QTextCharFormat &fmt, const QString &face)
{
    fmt.setProperty(PropFace, face);
    if (!face.isEmpty()) {
        QFont f = fmt.font();
        f.setFamily(face);
        fmt.setFont(f, QTextCharFormat::FontPropertiesSpecifiedOnly);
    }
}

QTextCharFormat InputEdit::defaultFormat() const
{
    QTextCharFormat fmt;
    const QString face = qs(purple_prefs_get_string(QTPURPLE_PREF_FONT_FACE));
    if (!face.isEmpty())
        setFontFace(fmt, face);
    const QString color = qs(purple_prefs_get_string(QTPURPLE_PREF_FONT_COLOR));
    if (QColor::isValidColor(color))
        fmt.setForeground(QColor(color));
    const int size = purple_prefs_get_int(QTPURPLE_PREF_FONT_SIZE);
    if (size >= 1 && size <= 7 && size != 3)
        setHtmlSize(fmt, size, font().pointSizeF());
    return fmt;
}

void InputEdit::resetInput()
{
    clear();
    setCurrentCharFormat(defaultFormat());
}

void InputEdit::addHistory()
{
    const QString key = document()->toHtml();
    if (m_historyKeys.isEmpty() || m_historyKeys.last() != key) {
        m_history.append(QTextDocumentFragment(document()));
        m_historyKeys.append(key);
        m_historyImages.append(imageNamesInDocument());
    }
    while (m_history.size() > 100) {
        m_history.removeFirst();
        m_historyKeys.removeFirst();
        m_historyImages.removeFirst();
    }
    m_historyPos = -1;
    m_draft = QTextDocumentFragment();
    pruneImages();
}

/* ------------------------------------------------------------- images */

QStringList InputEdit::imageNamesInDocument() const
{
    QStringList names;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (frag.isValid() && frag.charFormat().isImageFormat())
                names << frag.charFormat().toImageFormat().name();
        }
    }
    return names;
}

/* Keep image data only for images still in the entry, the draft or the
 * sent-message history. */
void InputEdit::pruneImages()
{
    QSet<QString> keep;
    for (const QString &n : imageNamesInDocument())
        keep.insert(n);
    for (const QStringList &names : std::as_const(m_historyImages))
        for (const QString &n : names)
            keep.insert(n);
    if (!m_draft.isEmpty()) {
        /* The draft is not inspected; keep everything while one exists. */
        return;
    }
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (keep.contains(it.key())) {
            ++it;
        } else {
            m_imageCache.remove(it.key());
            it = m_images.erase(it);
        }
    }
}

bool InputEdit::hasImages() const
{
    return !imageNamesInDocument().isEmpty();
}

bool InputEdit::insertEncoded(const QByteArray &data, const QString &filename, const QImage &image)
{
    if (data.isEmpty() || image.isNull())
        return false;
    if (data.size() > kMaxImageBytes) {
        QMessageBox::warning(this, tr("Insert Image"),
                             tr("%1 is too large to send inline (%2 MB; the limit is %3 MB).")
                                 .arg(filename)
                                 .arg(double(data.size()) / (1024.0 * 1024.0), 0, 'f', 1)
                                 .arg(kMaxImageBytes / (1024 * 1024)));
        return false;
    }

    const QString name = QStringLiteral("%1:%2").arg(QLatin1String(kImageScheme)).arg(++m_imageCounter);
    m_images.insert(name, Image{ data, filename });
    m_imageCache.insert(name, image);
    document()->addResource(QTextDocument::ImageResource, QUrl(name), image);

    QTextImageFormat fmt;
    fmt.setName(name);
    QSize shown = image.size();
    if (shown.width() > kPreviewWidth || shown.height() > kPreviewHeight)
        shown.scale(kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatio);
    fmt.setWidth(shown.width());
    fmt.setHeight(shown.height());
    fmt.setToolTip(tr("%1 (%2 x %3, %4 KB)")
                       .arg(filename).arg(image.width()).arg(image.height())
                       .arg((data.size() + 1023) / 1024));
    textCursor().insertImage(fmt);
    setFocus();
    return true;
}

bool InputEdit::insertImage(const QImage &image)
{
    QByteArray data;
    QString filename;
    if (image.isNull() || !encodeImage(image, &data, &filename)) {
        QMessageBox::warning(this, tr("Insert Image"), tr("The image could not be read."));
        return false;
    }
    return insertEncoded(data, filename, image);
}

void InputEdit::insertImageFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFile f(path);
        const QString name = QFileInfo(path).fileName();
        if (f.size() > kMaxImageBytes) {
            QMessageBox::warning(this, tr("Insert Image"),
                                 tr("%1 is too large to send inline (the limit is %2 MB).")
                                     .arg(name).arg(kMaxImageBytes / (1024 * 1024)));
            continue;
        }
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Insert Image"), tr("%1 could not be opened.").arg(name));
            continue;
        }
        /* Send the file as it is (keeps GIF animation and the original
         * compression); decode only for the preview. */
        const QByteArray data = f.readAll();
        QImage image;
        if (!image.loadFromData(data)) {
            QMessageBox::warning(this, tr("Insert Image"), tr("%1 is not an image Qt can read.").arg(name));
            continue;
        }
        insertEncoded(data, name, image);
    }
}

static QStringList localImageFiles(const QMimeData *source)
{
    QStringList files;
    if (!source->hasUrls())
        return files;
    for (const QUrl &url : source->urls()) {
        if (!url.isLocalFile())
            return QStringList();           /* mixed or remote: treat as text */
        const QString path = url.toLocalFile();
        if (QImageReader::imageFormat(path).isEmpty())
            return QStringList();           /* not only images */
        files << path;
    }
    return files;
}

bool InputEdit::canInsertFromMimeData(const QMimeData *source) const
{
    if (source->hasImage() || !localImageFiles(source).isEmpty())
        return true;
    return QTextEdit::canInsertFromMimeData(source);
}

void InputEdit::insertFromMimeData(const QMimeData *source)
{
    /* Local image files first: their original data is better than the
     * decoded copy some file managers also put on the clipboard. */
    const QStringList files = localImageFiles(source);
    if (!files.isEmpty()) {
        insertImageFiles(files);
        return;
    }
    if (source->hasImage()) {
        insertImage(qvariant_cast<QImage>(source->imageData()));
        return;
    }
    /* Text: plain only (setAcceptRichText(false)). */
    QTextEdit::insertFromMimeData(source);
}

QVariant InputEdit::loadResource(int type, const QUrl &name)
{
    /* Images recalled from the history after the document was cleared. */
    if (type == QTextDocument::ImageResource) {
        auto it = m_imageCache.constFind(name.toString());
        if (it != m_imageCache.constEnd())
            return it.value();
    }
    return QTextEdit::loadResource(type, name);
}

void InputEdit::loadHistory(int pos)
{
    clear();
    const QTextDocumentFragment frag = (pos < 0) ? m_draft : m_history.at(pos);
    if (!frag.isEmpty())
        textCursor().insertFragment(frag);
    moveCursor(QTextCursor::End);
}

void InputEdit::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !(mods & Qt::ShiftModifier)) {
        Q_EMIT sendRequested();
        return;
    }
    if (key == Qt::Key_Tab && mods == Qt::NoModifier) {
        if (m_tabCompletion)
            Q_EMIT completeRequested();
        else
            focusNextChild();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Up) {
        if (m_history.isEmpty())
            return;
        if (m_historyPos < 0) {
            m_draft = QTextDocumentFragment(document());
            m_historyPos = m_history.size() - 1;
        } else if (m_historyPos > 0) {
            --m_historyPos;
        }
        loadHistory(m_historyPos);
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Down) {
        if (m_historyPos < 0)
            return;
        ++m_historyPos;
        if (m_historyPos >= m_history.size())
            m_historyPos = -1;
        loadHistory(m_historyPos);
        return;
    }
    if (mods == Qt::ControlModifier && (key == Qt::Key_B || key == Qt::Key_I || key == Qt::Key_U)) {
        QTextCharFormat cur = currentCharFormat();
        QTextCharFormat fmt;
        if (key == Qt::Key_B)
            fmt.setFontWeight(cur.fontWeight() > QFont::Normal ? QFont::Normal : QFont::Bold);
        else if (key == Qt::Key_I)
            fmt.setFontItalic(!cur.fontItalic());
        else
            fmt.setFontUnderline(!cur.fontUnderline());
        mergeCurrentCharFormat(fmt);
        return;
    }
    QTextEdit::keyPressEvent(event);
}

QString InputEdit::toPurpleHtml(bool withFormatting, int flags,
                                QList<int> *imageIds, QList<Image> *notInline) const
{
    const bool html = withFormatting && (flags < 0 || (flags & PURPLE_CONNECTION_HTML));
    const bool noBack = flags >= 0 && (flags & PURPLE_CONNECTION_NO_BGCOLOR);
    const bool noSize = flags >= 0 && (flags & PURPLE_CONNECTION_NO_FONTSIZE);
    const bool noUrlDesc = flags >= 0 && (flags & PURPLE_CONNECTION_NO_URLDESC);
    /* Inline images need an HTML connection that does not refuse them;
     * flags < 0 (not connected) never sends anything, so be strict there. */
    const bool inlineImages = imageIds && flags >= 0 && (flags & PURPLE_CONNECTION_HTML) &&
                              !(flags & PURPLE_CONNECTION_NO_IMAGES);

    QString out;
    bool firstBlock = true;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (!firstBlock)
            out += QLatin1String("<br>");
        firstBlock = false;

        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QString text = frag.text();
            const QTextCharFormat fmt = frag.charFormat();

            if (fmt.isImageFormat()) {
                const QString name = fmt.toImageFormat().name();
                auto img = m_images.constFind(name);
                if (img == m_images.constEnd())
                    continue;                       /* data no longer available */
                /* One fragment can hold several copies of the same image. */
                const int copies = text.count(QChar(QChar::ObjectReplacementCharacter));
                for (int i = 0; i < copies; ++i) {
                    if (!inlineImages) {
                        if (notInline)
                            notInline->append(img.value());
                        continue;
                    }
                    const QByteArray &bytes = img.value().data;
                    gpointer copy = g_malloc(gsize(bytes.size()));
                    memcpy(copy, bytes.constData(), size_t(bytes.size()));
                    /* The image store takes ownership of copy. */
                    const int id = purple_imgstore_add_with_id(copy, size_t(bytes.size()),
                                                               img.value().filename.toUtf8().constData());
                    if (id <= 0) {
                        if (notInline)
                            notInline->append(img.value());
                        continue;
                    }
                    imageIds->append(id);
                    out += QStringLiteral("<img id=\"%1\">").arg(id);
                }
                continue;
            }

            if (!html) {
                out += escapeText(text);
                continue;
            }

            QString open, close;

            if (fmt.isAnchor() && !fmt.anchorHref().isEmpty()) {
                const QUrl url(fmt.anchorHref());
                if (allowedLinkScheme(url)) {
                    if (noUrlDesc) {
                        /* Only the address itself can be sent. */
                        out += escapeText(url.toString());
                        continue;
                    }
                    open += QStringLiteral("<a href=\"%1\">").arg(url.toString().toHtmlEscaped());
                    close.prepend(QLatin1String("</a>"));
                }
            }

            QStringList fontAttrs;
            const QString face = fontFace(fmt);
            if (!face.isEmpty())
                fontAttrs << QStringLiteral("face=\"%1\"").arg(face.toHtmlEscaped());
            if (!fmt.isAnchor() && fmt.hasProperty(QTextFormat::ForegroundBrush))
                fontAttrs << QStringLiteral("color=\"%1\"").arg(fmt.foreground().color().name());
            if (!noBack && fmt.hasProperty(QTextFormat::BackgroundBrush))
                fontAttrs << QStringLiteral("back=\"%1\"").arg(fmt.background().color().name());
            if (!noSize && fmt.hasProperty(PropHtmlSize) && htmlSize(fmt) != 3)
                fontAttrs << QStringLiteral("size=\"%1\"").arg(htmlSize(fmt));
            if (!fontAttrs.isEmpty()) {
                open += QStringLiteral("<font %1>").arg(fontAttrs.join(QLatin1Char(' ')));
                close.prepend(QLatin1String("</font>"));
            }

            const struct { bool on; const char *tag; } styles[] = {
                { fmt.fontWeight() > QFont::Normal, "b" },
                { fmt.fontItalic(), "i" },
                { fmt.fontUnderline() && !fmt.isAnchor(), "u" },
                { fmt.fontStrikeOut(), "s" },
            };
            for (const auto &st : styles) {
                if (!st.on)
                    continue;
                open += QStringLiteral("<%1>").arg(QLatin1String(st.tag));
                close.prepend(QStringLiteral("</%1>").arg(QLatin1String(st.tag)));
            }

            out += open + escapeText(text) + close;
        }
    }
    return out;
}

/* =============================================================== FormatBar */

FormatBar::FormatBar(InputEdit *edit, QWidget *parent)
    : QWidget(parent), m_edit(edit)
{
    m_bold = makeButton(QStringLiteral("B"), tr("Bold (Ctrl+B)"), true);
    QFont f = m_bold->font();
    f.setBold(true);
    m_bold->setFont(f);
    m_italic = makeButton(QStringLiteral("I"), tr("Italic (Ctrl+I)"), true);
    f = m_italic->font();
    f.setItalic(true);
    m_italic->setFont(f);
    m_underline = makeButton(QStringLiteral("U"), tr("Underline (Ctrl+U)"), true);
    f = m_underline->font();
    f.setUnderline(true);
    m_underline->setFont(f);
    m_strike = makeButton(QStringLiteral("S"), tr("Strikethrough"), true);
    f = m_strike->font();
    f.setStrikeOut(true);
    m_strike->setFont(f);
    m_larger = makeButton(QStringLiteral("A+"), tr("Larger font"), false);
    m_smaller = makeButton(QStringLiteral("A\u2212"), tr("Smaller font"), false);
    m_face = makeButton(tr("Font"), tr("Font face"), false);
    m_color = makeButton(QStringLiteral("A"), tr("Text color"), false);
    m_background = makeButton(QStringLiteral("\u2B1B"), tr("Background color"), false);
    m_reset = makeButton(QStringLiteral("\u2205"), tr("Reset formatting"), false);
    m_link = makeButton(tr("Link"), tr("Insert link"), false);
    m_image = makeButton(tr("Image"), tr("Insert image (you can also paste or drop images)"), false);
    m_emoji = makeButton(QStringLiteral("\U0001F642"), tr("Insert emoji"), false);

    connect(m_bold, &QToolButton::toggled, this, &FormatBar::toggleBold);
    connect(m_italic, &QToolButton::toggled, this, &FormatBar::toggleItalic);
    connect(m_underline, &QToolButton::toggled, this, &FormatBar::toggleUnderline);
    connect(m_strike, &QToolButton::toggled, this, &FormatBar::toggleStrike);
    connect(m_larger, &QToolButton::clicked, this, &FormatBar::growFont);
    connect(m_smaller, &QToolButton::clicked, this, &FormatBar::shrinkFont);
    connect(m_face, &QToolButton::clicked, this, &FormatBar::chooseFace);
    connect(m_color, &QToolButton::clicked, this, &FormatBar::chooseColor);
    connect(m_background, &QToolButton::clicked, this, &FormatBar::chooseBackground);
    connect(m_reset, &QToolButton::clicked, this, &FormatBar::resetFormatting);
    connect(m_link, &QToolButton::clicked, this, &FormatBar::insertLink);
    connect(m_image, &QToolButton::clicked, this, &FormatBar::insertImage);

    /* Emoji picker: a grid of buttons in a drop-down. */
    QMenu *menu = new QMenu(m_emoji);
    QWidget *grid = new QWidget(menu);
    QGridLayout *gl = new QGridLayout(grid);
    gl->setSpacing(1);
    gl->setContentsMargins(4, 4, 4, 4);
    const QList<QPair<QString, QString>> entries = Emoji::pickerEntries();
    int i = 0;
    for (const auto &e : entries) {
        QToolButton *b = new QToolButton(grid);
        b->setAutoRaise(true);
        b->setText(e.second);
        b->setToolTip(e.first);
        QFont bf = b->font();
        if (bf.pointSizeF() > 0)
            bf.setPointSizeF(bf.pointSizeF() * 1.3);
        b->setFont(bf);
        const QString glyph = e.second;
        connect(b, &QToolButton::clicked, this, [this, menu, glyph]() {
            menu->close();
            m_edit->textCursor().insertText(glyph);
            m_edit->setFocus();
        });
        gl->addWidget(b, i / 8, i % 8);
        ++i;
    }
    QWidgetAction *wa = new QWidgetAction(menu);
    wa->setDefaultWidget(grid);
    menu->addAction(wa);
    m_emoji->setMenu(menu);
    m_emoji->setPopupMode(QToolButton::InstantPopup);

    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(1);
    for (QToolButton *b : { m_bold, m_italic, m_underline, m_strike })
        lay->addWidget(b);
    lay->addSpacing(6);
    for (QToolButton *b : { m_larger, m_smaller, m_face, m_color, m_background, m_reset })
        lay->addWidget(b);
    lay->addSpacing(6);
    lay->addWidget(m_link);
    lay->addWidget(m_image);
    lay->addWidget(m_emoji);
    lay->addStretch(1);

    connect(m_edit, &QTextEdit::currentCharFormatChanged, this, &FormatBar::syncButtons);
    syncButtons();
}

QToolButton *FormatBar::makeButton(const QString &text, const QString &tip, bool checkable)
{
    QToolButton *b = new QToolButton(this);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);     /* keep the cursor in the entry */
    return b;
}

void FormatBar::setConnectionFlags(int flags)
{
    m_flags = flags;
    const bool html = flags < 0 || (flags & PURPLE_CONNECTION_HTML);
    for (QToolButton *b : { m_bold, m_italic, m_underline, m_strike, m_face, m_color, m_reset, m_link })
        b->setEnabled(html);
    const bool size = html && !(flags >= 0 && (flags & PURPLE_CONNECTION_NO_FONTSIZE));
    m_larger->setEnabled(size);
    m_smaller->setEnabled(size);
    m_background->setEnabled(html && !(flags >= 0 && (flags & PURPLE_CONNECTION_NO_BGCOLOR)));
    const QString plainTip = tr("This protocol only sends plain text.");
    for (QToolButton *b : { m_bold, m_italic, m_underline, m_strike, m_face, m_color, m_link })
        b->setStatusTip(html ? QString() : plainTip);

    /* Images stay available: when they cannot go inline, sending offers a
     * file transfer instead (IMs). Say so in the tooltip. */
    const bool inlineOk = flags < 0 || ((flags & PURPLE_CONNECTION_HTML) &&
                                        !(flags & PURPLE_CONNECTION_NO_IMAGES));
    m_image->setToolTip(inlineOk
        ? tr("Insert image (you can also paste or drop images)")
        : tr("Insert image. This protocol cannot send images inside messages; "
             "in an IM they can be sent as a file transfer instead."));
}

void FormatBar::insertImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Insert Image"), QString(),
                                                            imageFileFilter());
    if (!files.isEmpty())
        m_edit->insertImageFiles(files);
}

void FormatBar::syncButtons()
{
    const QTextCharFormat fmt = m_edit->currentCharFormat();
    const QSignalBlocker b1(m_bold), b2(m_italic), b3(m_underline), b4(m_strike);
    m_bold->setChecked(fmt.fontWeight() > QFont::Normal);
    m_italic->setChecked(fmt.fontItalic());
    m_underline->setChecked(fmt.fontUnderline() && !fmt.isAnchor());
    m_strike->setChecked(fmt.fontStrikeOut());
}

void FormatBar::merge(const QTextCharFormat &fmt)
{
    m_edit->mergeCurrentCharFormat(fmt);
    m_edit->setFocus();
}

void FormatBar::toggleBold(bool on)
{
    QTextCharFormat fmt;
    fmt.setFontWeight(on ? QFont::Bold : QFont::Normal);
    merge(fmt);
}

void FormatBar::toggleItalic(bool on)
{
    QTextCharFormat fmt;
    fmt.setFontItalic(on);
    merge(fmt);
}

void FormatBar::toggleUnderline(bool on)
{
    QTextCharFormat fmt;
    fmt.setFontUnderline(on);
    merge(fmt);
}

void FormatBar::toggleStrike(bool on)
{
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(on);
    merge(fmt);
}

void FormatBar::changeSize(int delta)
{
    QTextCharFormat fmt;
    const int size = InputEdit::htmlSize(m_edit->currentCharFormat()) + delta;
    InputEdit::setHtmlSize(fmt, size, m_edit->font().pointSizeF());
    merge(fmt);
}

void FormatBar::growFont()
{
    changeSize(+1);
}

void FormatBar::shrinkFont()
{
    changeSize(-1);
}

void FormatBar::chooseFace()
{
    bool ok = false;
    QFont initial = m_edit->currentCharFormat().font();
    const QFont chosen = QFontDialog::getFont(&ok, initial, this, tr("Font Face"));
    if (!ok)
        return;
    QTextCharFormat fmt;
    InputEdit::setFontFace(fmt, chosen.family());
    merge(fmt);
}

void FormatBar::chooseColor()
{
    const QColor c = QColorDialog::getColor(m_edit->currentCharFormat().foreground().color(), this,
                                            tr("Text Color"));
    if (!c.isValid())
        return;
    QTextCharFormat fmt;
    fmt.setForeground(c);
    merge(fmt);
}

void FormatBar::chooseBackground()
{
    const QColor c = QColorDialog::getColor(Qt::yellow, this, tr("Background Color"));
    if (!c.isValid())
        return;
    QTextCharFormat fmt;
    fmt.setBackground(c);
    merge(fmt);
}

void FormatBar::resetFormatting()
{
    const QTextCharFormat def = m_edit->defaultFormat();
    QTextCursor cur = m_edit->textCursor();
    if (cur.hasSelection())
        cur.setCharFormat(def);
    m_edit->setCurrentCharFormat(def);
    m_edit->setFocus();
}

void FormatBar::insertLink()
{
    QTextCursor cur = m_edit->textCursor();
    bool ok = false;
    const QString address = QInputDialog::getText(this, tr("Insert Link"), tr("URL:"),
                                                   QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || address.isEmpty())
        return;
    const QUrl url = QUrl::fromUserInput(address);
    if (!url.isValid() || !allowedLinkScheme(url)) {
        QMessageBox::warning(this, tr("Insert Link"),
                             tr("Only http, https, ftp, mailto, xmpp and irc links can be inserted."));
        return;
    }
    QString label = cur.selectedText();
    if (label.isEmpty()) {
        label = QInputDialog::getText(this, tr("Insert Link"), tr("Description (optional):"),
                                      QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok)
            return;
    }
    if (label.isEmpty())
        label = url.toString();

    QTextCharFormat link = m_edit->currentCharFormat();
    link.setAnchor(true);
    link.setAnchorHref(url.toString());
    link.setFontUnderline(true);
    link.setForeground(palette().color(QPalette::Link));
    cur.insertText(label, link);

    /* Continue typing with the default format, not the link's. */
    m_edit->setTextCursor(cur);
    m_edit->setCurrentCharFormat(m_edit->defaultFormat());
    m_edit->setFocus();
}
