/*
 * QtPurple - a Qt front end for libpurple
 *
 * Plugin manager with a generic preference editor for plugins that
 * provide a PurplePluginPrefFrame.
 */
#ifndef QTPURPLE_PLUGINSDIALOG_H
#define QTPURPLE_PLUGINSDIALOG_H

#include "common.h"

#include <QDialog>

class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

class PluginsDialog : public QDialog
{
    Q_OBJECT
public:
    static void showDialog();

private Q_SLOTS:
    void refresh();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onSelectionChanged();
    void onConfigure();

private:
    explicit PluginsDialog(QWidget *parent = nullptr);
    PurplePlugin *selectedPlugin() const;

    QTreeWidget *m_list;
    QTextBrowser *m_details;
    QPushButton *m_configure;
    bool m_filling = false;
};

class PluginPrefsDialog : public QDialog
{
    Q_OBJECT
public:
    PluginPrefsDialog(PurplePlugin *plugin, PurplePluginPrefFrame *frame, QWidget *parent = nullptr);
    ~PluginPrefsDialog() override;

private:
    PurplePluginPrefFrame *m_frame;
};

#endif
