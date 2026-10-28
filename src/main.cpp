/*
 * QtPurple - a Qt front end for libpurple
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "common.h"
#include "core.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>

#include <csignal>

int main(int argc, char *argv[])
{
#if !defined(_WIN32)
    /* libpurple's DNS resolver forks helper processes; let the system reap
     * them (same approach as libpurple's nullclient example). */
    signal(SIGCHLD, SIG_IGN);
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(QTPURPLE_NAME));
    QApplication::setApplicationVersion(QStringLiteral(QTPURPLE_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Qt instant messenger built on libpurple"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOpt({ QStringLiteral("c"), QStringLiteral("config") },
                                 QObject::tr("Use <dir> for configuration files (default ~/.qtpurple)."),
                                 QObject::tr("dir"));
    QCommandLineOption debugOpt({ QStringLiteral("d"), QStringLiteral("debug") },
                                QObject::tr("Print libpurple debugging messages to stdout."));
    QCommandLineOption noLoginOpt({ QStringLiteral("n"), QStringLiteral("nologin") },
                                  QObject::tr("Start offline (do not sign on)."));
    parser.addOption(configOpt);
    parser.addOption(debugOpt);
    parser.addOption(noLoginOpt);
    parser.process(app);

    Core::Options options;
    options.configDir = parser.value(configOpt);
    options.debug = parser.isSet(debugOpt);
    options.noLogin = parser.isSet(noLoginOpt);

    QString error;
    if (!Core::init(options, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral(QTPURPLE_NAME), error);
        return 1;
    }

    const int rc = app.exec();
    Core::shutdown();
    return rc;
}
