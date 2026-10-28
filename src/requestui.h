/*
 * QtPurple - a Qt front end for libpurple
 *
 * PurpleRequestUiOps: input, choice, action, multi-field, file and
 * folder requests.
 *
 * Life cycle of every request window:
 *   - the user answers  -> the matching libpurple callback runs once,
 *                          then purple_request_close() is called, which
 *                          calls close_request() below;
 *   - libpurple closes  -> close_request() hides and deletes the window
 *                          without running any callback.
 */
#ifndef QTPURPLE_REQUESTUI_H
#define QTPURPLE_REQUESTUI_H

#include "common.h"

#include <QDialog>
#include <QFileDialog>
#include <QHash>
#include <QList>
#include <QPixmap>

#include <functional>

namespace RequestUi {
PurpleRequestUiOps *uiOps();
}

class RequestDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RequestDialog(PurpleRequestType type, QWidget *parent = nullptr);
    ~RequestDialog() override;

    /* Called from close_request(): no callbacks, just go away. */
    virtual void closeFromCore();

protected:
    /* Runs the callback (once) and closes the request. */
    void complete(const std::function<void()> &callback);
    /* Esc / window close. */
    void reject() override;
    /* Subclasses run their cancel callback via complete(). */
    virtual void cancelled() = 0;

    /* Shared header: primary text, secondary text, account context. */
    QWidget *makeHeader(const char *primary, const char *secondary, PurpleAccount *account,
                        const QPixmap &icon = QPixmap());

    PurpleRequestType m_type;
    bool m_done = false;
    bool m_closed = false;
};

class FileRequest : public QFileDialog
{
    Q_OBJECT
public:
    FileRequest(PurpleRequestType type, const char *title, const char *path, bool save,
                GCallback okCb, GCallback cancelCb, void *userData);
    ~FileRequest() override;
    void closeFromCore();

protected:
    void done(int result) override;

private:
    PurpleRequestType m_type;
    GCallback m_okCb;
    GCallback m_cancelCb;
    void *m_userData;
    bool m_finished = false;
    bool m_closed = false;
};

#endif
