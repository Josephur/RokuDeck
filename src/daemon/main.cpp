// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_service.h"

#include <QCoreApplication>
#include <QDBusConnection>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rokudeckd"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.josephur"));

    RokuDeck::DaemonService daemon(QDBusConnection::sessionBus());
    if (!daemon.registerOnBus()) {
        return 1;
    }
    return application.exec();
}
