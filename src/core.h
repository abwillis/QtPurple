/*
 * QtPurple - a Qt front end for libpurple
 *
 * libpurple core start-up / shutdown, core, account and connection UI ops.
 */
#ifndef QTPURPLE_CORE_H
#define QTPURPLE_CORE_H

#include "common.h"

#include <QString>

namespace Core {

struct Options {
    QString configDir;      /* empty: ~/.qtpurple */
    bool debug = false;     /* libpurple debug output on stdout */
    bool noLogin = false;   /* start with the offline status */
};

/* Initialise libpurple and the UI. Returns false on failure. */
bool init(const Options &options, QString *error);

/* Leave the Qt event loop; main() then calls shutdown(). */
void requestQuit();

/* Uninitialise libpurple. Call after QApplication::exec() returned. */
void shutdown();

bool shuttingDown();

/* Connect an account immediately (used by "Reconnect" buttons). */
void reconnectNow(PurpleAccount *account);

} // namespace Core

#endif
