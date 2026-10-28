/*
 * QtPurple - a Qt front end for libpurple
 *
 * System tray icon: shows the global status, flags unread conversations,
 * toggles the buddy list and offers a quick menu.
 */
#ifndef QTPURPLE_TRAYICON_H
#define QTPURPLE_TRAYICON_H

#include "common.h"

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;
class QTimer;

class TrayIcon : public QObject
{
    Q_OBJECT
public:
    static TrayIcon *instance();

    /* Create or remove the icon according to "/qtpurple/tray/enabled". */
    void applyPrefs();

    /* True when the icon is shown, i.e. closing the buddy list only hides it. */
    bool isShown() const;

    void shutdown();

public Q_SLOTS:
    void update();

private Q_SLOTS:
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void onBlink();
    void populateMenu();

private:
    TrayIcon();
    void toggleBuddyList();
    void refreshIcon();
    QIcon makeIcon(PurpleStatusPrimitive prim, bool connecting, bool message) const;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QTimer *m_blinkTimer;
    bool m_blinkPhase = false;
    bool m_shutdown = false;
};

#endif
