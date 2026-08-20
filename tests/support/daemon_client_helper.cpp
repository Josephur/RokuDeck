// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 4) {
        return 2;
    }
    bool intervalValid = false;
    const uint intervalMs = application.arguments().at(3).toUInt(&intervalValid);
    if (!intervalValid || intervalMs == 0) {
        return 2;
    }
    QDBusInterface daemon(QStringLiteral("io.github.josephur.RokuDeck1"),
                          QStringLiteral("/io/github/josephur/RokuDeck1"),
                          QStringLiteral("io.github.josephur.RokuDeck1"),
                          QDBusConnection::sessionBus());
    auto* watcher = new QDBusPendingCallWatcher(
        daemon.asyncCall(QStringLiteral("RegisterClient"), application.arguments().at(1),
                         QStringList{application.arguments().at(2)}, intervalMs),
        &application);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &application,
                     [&application, watcher](QDBusPendingCallWatcher*) {
                         if (watcher->isError()) {
                             application.exit(1);
                             return;
                         }
                         QTextStream(stdout) << QStringLiteral("registered") << Qt::endl;
                     });
    return application.exec();
}
