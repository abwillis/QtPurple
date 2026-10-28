/*
 * QtPurple - a Qt front end for libpurple
 *
 * Account manager and account editor.
 */
#ifndef QTPURPLE_ACCOUNTSDIALOG_H
#define QTPURPLE_ACCOUNTSDIALOG_H

#include "common.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class AccountsDialog : public QDialog
{
    Q_OBJECT
public:
    static void showDialog();
    static void editAccount(PurpleAccount *account);   /* nullptr: new account */

    void scheduleRefresh();

private Q_SLOTS:
    void refresh();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onSelectionChanged();
    void onAdd();
    void onModify();
    void onDelete();

private:
    explicit AccountsDialog(QWidget *parent = nullptr);
    PurpleAccount *selectedAccount() const;

    QTreeWidget *m_list;
    QPushButton *m_modify;
    QPushButton *m_delete;
    bool m_filling = false;
    bool m_refreshPending = false;
};

class AccountEditor : public QDialog
{
    Q_OBJECT
public:
    explicit AccountEditor(PurpleAccount *account, QWidget *parent = nullptr);
    PurpleAccount *account() const { return m_account; }

protected:
    void accept() override;

private Q_SLOTS:
    void rebuildProtocolWidgets();

private:
    struct SplitWidget {
        PurpleAccountUserSplit *split;
        QLineEdit *edit;
    };
    struct OptionWidget {
        PurpleAccountOption *option;
        QWidget *widget;
    };

    PurplePlugin *currentPrpl() const;
    QString composeUsername() const;

    PurpleAccount *m_account;        /* nullptr while creating */
    QComboBox *m_protocol;
    QLineEdit *m_username;
    QFormLayout *m_splitLayout;
    QWidget *m_splitBox;
    QLineEdit *m_password;
    QLabel *m_passwordLabel;
    QCheckBox *m_remember;
    QLineEdit *m_alias;
    QCheckBox *m_checkMail;
    QCheckBox *m_register;
    QFormLayout *m_optionLayout;
    QWidget *m_optionBox;

    QList<SplitWidget> m_splits;
    QList<OptionWidget> m_options;
};

#endif
