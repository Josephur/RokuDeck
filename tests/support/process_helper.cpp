// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QFile>
#include <QThread>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--emit-output"))) {
        QFile standardOutput;
        if (!standardOutput.open(stdout, QIODevice::WriteOnly)) {
            return 2;
        }
        standardOutput.write(QByteArray(8192, 'o'));
        standardOutput.close();

        QFile standardError;
        if (!standardError.open(stderr, QIODevice::WriteOnly)) {
            return 3;
        }
        standardError.write(QByteArray(8192, 'e'));
        standardError.close();
        return 0;
    }
    if (arguments.contains(QStringLiteral("--hang"))) {
        QThread::msleep(1000);
    }
    return 0;
}
