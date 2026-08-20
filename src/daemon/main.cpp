// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_service.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rokudeckd"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.josephur"));

    RokuDeck::DaemonService daemon(QDBusConnection::sessionBus());
    QObject::connect(&daemon, &RokuDeck::DaemonService::idleExitRequested, &application,
                     &QCoreApplication::quit);
    if (!daemon.registerOnBus()) {
        QTextStream(stderr) << QStringLiteral("rokudeckd could not register on D-Bus: ")
                            << daemon.lastRegistrationError() << Qt::endl;
        return 1;
    }
    return application.exec();
}
