// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/poll_scheduler.h"

#include <QtTest>

using namespace RokuDeck;
using namespace std::chrono_literals;

class PollSchedulerTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void mergesInterestsBySerialAtShortestInterval();
    void failedDeviceDoesNotDeferHealthyDevice();
    void backoffIsBoundedAndUsesInjectedJitter();
};

void PollSchedulerTest::mergesInterestsBySerialAtShortestInterval()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget-a"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power"), QStringLiteral("app")}, 5s});
    scheduler.setInterest({QStringLiteral("automation-a"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power"), QStringLiteral("audio")}, 1s});

    QCOMPARE(scheduler.deviceCount(), 1);
    QCOMPARE(scheduler.intervalFor(QStringLiteral("SERIAL-A")), 1s);
    QCOMPARE(scheduler.fieldsFor(QStringLiteral("SERIAL-A")),
             QSet<QString>({QStringLiteral("power"), QStringLiteral("app"), QStringLiteral("audio")}));
    now += 1s;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::failedDeviceDoesNotDeferHealthyDevice()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-UNAVAILABLE"),
                           {QStringLiteral("power")}, 100ms});
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-HEALTHY"),
                           {QStringLiteral("power")}, 100ms});
    now += 100ms;
    QCOMPARE(scheduler.dueDevices().size(), 2);

    scheduler.recordResult(QStringLiteral("SERIAL-UNAVAILABLE"), false);
    scheduler.recordResult(QStringLiteral("SERIAL-HEALTHY"), true);
    now += 100ms;

    const QStringList due = scheduler.dueDevices();
    QVERIFY(due.contains(QStringLiteral("SERIAL-HEALTHY")));
    QVERIFY(!due.contains(QStringLiteral("SERIAL-UNAVAILABLE")));
}

void PollSchedulerTest::backoffIsBoundedAndUsesInjectedJitter()
{
    PollScheduler::TimePoint now{};
    int jitterCalls = 0;
    PollScheduler scheduler(
        [&] { return now; },
        [&](auto delay) {
            ++jitterCalls;
            return delay + 10ms;
        });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});

    for (int attempt = 0; attempt < 20; ++attempt) {
        scheduler.recordResult(QStringLiteral("SERIAL-A"), false);
    }

    QCOMPARE(jitterCalls, 20);
    QVERIFY(scheduler.delayFor(QStringLiteral("SERIAL-A")) <= PollScheduler::maximumBackoff());
}

QTEST_GUILESS_MAIN(PollSchedulerTest)
#include "poll_scheduler_test.moc"
