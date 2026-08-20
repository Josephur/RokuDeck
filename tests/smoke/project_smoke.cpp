// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

class ProjectSmokeTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void qtEventLoopIsAvailable() { QVERIFY(QCoreApplication::instance()); }
};

QTEST_GUILESS_MAIN(ProjectSmokeTest)
#include "project_smoke.moc"
