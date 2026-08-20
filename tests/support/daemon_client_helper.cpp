// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QDBusInterface daemon(QStringLiteral("io.github.josephur.RokuDeck1"),
                          QStringLiteral("/io/github/josephur/RokuDeck1"),
                          QStringLiteral("io.github.josephur.RokuDeck1"),
                          QDBusConnection::sessionBus());
    auto* watcher = new QDBusPendingCallWatcher(
        daemon.asyncCall(QStringLiteral("RegisterClient"), QStringLiteral("widget-owner"),
                         QStringList{QStringLiteral("SERIAL-A")}, 1200u),
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
