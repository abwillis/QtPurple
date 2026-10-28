/*
 * QtPurple - a Qt front end for libpurple
 *
 * Dialogs used by the buddy list: account selector, add buddy,
 * add/join chat and the simple "account + name" prompts.
 */
#ifndef QTPURPLE_BLISTDIALOGS_H
#define QTPURPLE_BLISTDIALOGS_H

#include "common.h"

#include <QComboBox>
#include <QDialog>
#include <QList>

#include <functional>

class QDialogButtonBox;
class QFormLayout;
class QLineEdit;

/* Combo box listing accounts; refreshes itself on sign-on/sign-off. */
class AccountCombo : public QComboBox
{
    Q_OBJECT
public:
    using Filter = std::function<bool(PurpleAccount *)>;

    explicit AccountCombo(bool onlineOnly, QWidget *parent = nullptr, Filter filter = Filter());
    ~AccountCombo() override;

    PurpleAccount *account() const;      /* validated, or nullptr */
    void setAccount(PurpleAccount *account);
    void refill();

Q_SIGNALS:
    void accountChanged(PurpleAccount *account);

private:
    bool m_onlineOnly;
    Filter m_filter;
    bool m_filling = false;
};

/* Editable combo with all buddy list groups. */
class GroupCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit GroupCombo(QWidget *parent = nullptr);
    QString groupName() const;
};

class AddBuddyDialog : public QDialog
{
    Q_OBJECT
public:
    AddBuddyDialog(PurpleAccount *account, const QString &user, const QString &group,
                   const QString &alias, QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    AccountCombo *m_account;
    QLineEdit *m_user;
    QLineEdit *m_alias;
    QLineEdit *m_invite;
    GroupCombo *m_group;
};

class ChatDialog : public QDialog
{
    Q_OBJECT
public:
    enum Mode { AddChat, JoinChat };

    ChatDialog(Mode mode, PurpleAccount *account, const QString &group,
               const QString &alias, const QString &name, QWidget *parent = nullptr);

protected:
    void accept() override;

private Q_SLOTS:
    void rebuildFields();

private:
    struct Field {
        QByteArray identifier;
        QString label;
        QWidget *widget;
        bool required;
        bool isInt;
    };

    GHashTable *collectComponents(QString *error) const;

    Mode m_mode;
    QString m_initialName;
    AccountCombo *m_account;
    QFormLayout *m_fieldsLayout;
    QWidget *m_fieldsBox;
    QList<Field> m_fields;
    QLineEdit *m_alias = nullptr;
    GroupCombo *m_group = nullptr;
};

/* Generic "pick an account and type a name" dialog (New IM, Get Info). */
class AccountNameDialog : public QDialog
{
    Q_OBJECT
public:
    using Callback = std::function<void(PurpleAccount *, const QString &)>;

    AccountNameDialog(const QString &title, const QString &prompt,
                      Callback callback, QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    AccountCombo *m_account;
    QLineEdit *m_name;
    Callback m_callback;
};

#endif
