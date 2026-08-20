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
        const qsizetype pidFileIndex = arguments.indexOf(QStringLiteral("--pid-file"));
        if (pidFileIndex >= 0 && pidFileIndex + 1 < arguments.size()) {
            QFile pidFile(arguments.at(pidFileIndex + 1));
            if (!pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return 4;
            }
            pidFile.write(QByteArray::number(QCoreApplication::applicationPid()));
            pidFile.close();
        }
        QThread::msleep(1000);
    }
    return 0;
}
