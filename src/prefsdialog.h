/*
 * QtPurple - a Qt front end for libpurple
 *
 * Preferences: buddy list, conversations, logging, status/idle, sounds,
 * network, proxy and system tray. Changes apply immediately (libpurple
 * preferences), like Pidgin.
 */
#ifndef QTPURPLE_PREFSDIALOG_H
#define QTPURPLE_PREFSDIALOG_H

#include "common.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    static void showDialog();

private Q_SLOTS:
    void onSoundItemChanged(QTreeWidgetItem *item, int column);
    void onSoundSelectionChanged();
    void onBrowseSound();
    void onResetSound();
    void onTestSound();
    void updateSoundWidgets();

private:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    QWidget *buildSoundsPage();
    QWidget *buildIdlePage();
    QWidget *buildTrayPage();
    QWidget *buildConversationsPage();
    QWidget *buildBuddyListPage();
    QWidget *buildLoggingPage();
    QWidget *buildNetworkPage();
    QWidget *buildProxyPage();
    void fillSoundList();
    int selectedEvent() const;

    QComboBox *m_method = nullptr;
    QLineEdit *m_command = nullptr;
    QSlider *m_volume = nullptr;
    QTreeWidget *m_soundList = nullptr;
    QPushButton *m_browse = nullptr;
    QPushButton *m_reset = nullptr;
    QPushButton *m_test = nullptr;
    bool m_filling = false;
};

#endif
