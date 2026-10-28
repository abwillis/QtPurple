/*
 * QtPurple - a Qt front end for libpurple
 *
 * Privacy settings and allow/block lists, PurplePrivacyUiOps, and the
 * Block/Unblock helpers used by the buddy and conversation menus.
 */
#ifndef QTPURPLE_PRIVACYDIALOG_H
#define QTPURPLE_PRIVACYDIALOG_H

#include "common.h"

#include <QDialog>

class AccountCombo;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Privacy {

PurplePrivacyUiOps *uiOps();

/* TRUE when messages from who are currently blocked for this account. */
bool isBlocked(PurpleAccount *account, const QString &who);

/* Block or unblock one user (keeps the account's privacy mode working
 * the way the user expects, like Pidgin's Block/Unblock). */
void setBlocked(PurpleAccount *account, const QString &who, bool block);

} // namespace Privacy

class PrivacyDialog : public QDialog
{
    Q_OBJECT
public:
    static void showDialog(PurpleAccount *account = nullptr);
    void scheduleRefresh(PurpleAccount *account);

private Q_SLOTS:
    void refresh();
    void onModeChosen(int index);
    void addAllow();
    void removeAllow();
    void addBlock();
    void removeBlock();

private:
    explicit PrivacyDialog(QWidget *parent = nullptr);
    PurpleAccount *account() const;
    void addTo(bool allowList);
    void removeFrom(bool allowList);

    AccountCombo *m_account;
    QComboBox *m_mode;
    QLabel *m_note;
    QListWidget *m_allow;
    QListWidget *m_block;
    QPushButton *m_allowAdd;
    QPushButton *m_allowRemove;
    QPushButton *m_blockAdd;
    QPushButton *m_blockRemove;
    bool m_refreshPending = false;
    bool m_filling = false;
};

#endif
