/*
 * QtPurple - a Qt front end for libpurple
 *
 * PurpleNotifyUiOps: messages, e-mail, formatted text, user info,
 * search results and URIs.
 */
#ifndef QTPURPLE_NOTIFYUI_H
#define QTPURPLE_NOTIFYUI_H

#include "common.h"

#include <QDialog>

class QTreeWidget;

namespace NotifyUi {
PurpleNotifyUiOps *uiOps();
}

class NotifyDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NotifyDialog(PurpleNotifyType type, QWidget *parent = nullptr);
    ~NotifyDialog() override;

    /* Called from close_notify(). */
    virtual void closeFromCore();

protected:
    void done(int result) override;

    PurpleNotifyType m_type;
    bool m_closing = false;
    bool m_deleted = false;
};

class SearchResultsDialog : public NotifyDialog
{
    Q_OBJECT
public:
    SearchResultsDialog(PurpleConnection *gc, const char *title, const char *primary,
                        const char *secondary, PurpleNotifySearchResults *results,
                        gpointer userData);
    void setRows(PurpleNotifySearchResults *results);
    void closeFromCore() override;

private:
    PurpleAccount *m_account;
    PurpleNotifySearchResults *m_results;
    gpointer m_userData;
    QTreeWidget *m_tree;
};

#endif
