/*
 * QtPurple - a Qt front end for libpurple
 *
 * Debug window fed by PurpleDebugUiOps.
 */
#ifndef QTPURPLE_DEBUGWINDOW_H
#define QTPURPLE_DEBUGWINDOW_H

#include "common.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;

class DebugWindow : public QWidget
{
    Q_OBJECT
public:
    static DebugWindow *instance();
    static bool isActive();
    static PurpleDebugUiOps *uiOps();

    void append(PurpleDebugLevel level, const char *category, const char *text);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    explicit DebugWindow(QWidget *parent = nullptr);

    QPlainTextEdit *m_text;
    QCheckBox *m_pause;
    QComboBox *m_level;
};

#endif
