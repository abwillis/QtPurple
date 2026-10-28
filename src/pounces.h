/*
 * QtPurple - a Qt front end for libpurple
 *
 * Buddy pounces: the pounce handler (actions run when a pounce fires),
 * the pounce manager and the pounce editor.
 *
 * Action names and attributes are the same as Pidgin's, so the format of
 * pounces.xml matches; pounces are owned per UI, though, so pounces made
 * in Pidgin ("gtk-gaim") are not run by QtPurple and vice versa.
 */
#ifndef QTPURPLE_POUNCES_H
#define QTPURPLE_POUNCES_H

#include "common.h"

#include <QDialog>
#include <QList>

class AccountCombo;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace Pounces {

/* Register the pounce handler. Must run before purple_pounces_load(). */
void init();

/* TRUE when the pointer still refers to an existing pounce. */
bool exists(PurplePounce *pounce);

void showManager();

/* Open the editor; pounce == nullptr creates a new pounce, optionally
 * pre-filled with account/buddy. */
void editPounce(PurplePounce *pounce, PurpleAccount *account = nullptr,
                const QString &buddy = QString());

} // namespace Pounces

class PouncesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PouncesDialog(QWidget *parent = nullptr);
    void scheduleRefresh();

private Q_SLOTS:
    void refresh();
    void onSelectionChanged();
    void onAdd();
    void onModify();
    void onDelete();

private:
    PurplePounce *selectedPounce() const;

    QTreeWidget *m_list;
    QPushButton *m_modify;
    QPushButton *m_delete;
    bool m_refreshPending = false;
};

class PounceEditor : public QDialog
{
    Q_OBJECT
public:
    PounceEditor(PurplePounce *pounce, PurpleAccount *account, const QString &buddy,
                 QWidget *parent = nullptr);
    PurplePounce *pounce() const { return m_pounce; }

protected:
    void accept() override;

private:
    struct EventBox {
        PurplePounceEvent event;
        QCheckBox *box;
    };

    PurplePounce *m_pounce;      /* nullptr while creating */
    AccountCombo *m_account;
    QLineEdit *m_buddy;
    QList<EventBox> m_events;
    QCheckBox *m_onlyAway;
    QCheckBox *m_openWindow;
    QCheckBox *m_popup;
    QCheckBox *m_sendMessage;
    QLineEdit *m_message;
    QCheckBox *m_execute;
    QLineEdit *m_command;
    QCheckBox *m_playSound;
    QLineEdit *m_soundFile;
    QCheckBox *m_recurring;
};

#endif
