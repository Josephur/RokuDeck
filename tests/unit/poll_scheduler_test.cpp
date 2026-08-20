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
    void dueDeviceIsClaimedUntilResultArrives();
    void repeatedInterestPreservesDeadlineAndBackoff();
    void normalIntervalMayExceedBackoffCeiling();
    void backoffGrowthUsesExactJitterAndSuccessResetsIt();
    void schedulingNearClockMaximumDoesNotOverflow();
    void removesObsoleteInterestsAndLastDeviceSchedule();
    void repeatedHealthyInterestDoesNotPostponeDeadline();
    void removeAndReaddPreservesActiveClaim();
    void schedulingAtClockMinimumDoesNotOverflow();
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

void PollSchedulerTest::dueDeviceIsClaimedUntilResultArrives()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 100ms});
    now += 100ms;

    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
    QVERIFY(scheduler.dueDevices().isEmpty());
    scheduler.recordResult(QStringLiteral("SERIAL-A"), true);
    now += 100ms;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::repeatedInterestPreservesDeadlineAndBackoff()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    const PollInterest interest{QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                                {QStringLiteral("power")}, 1s};
    scheduler.setInterest(interest);
    now += 1s;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
    scheduler.recordResult(QStringLiteral("SERIAL-A"), false);
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 2s);

    scheduler.setInterest(interest);
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 2s);
    now += 1s;
    QVERIFY(scheduler.dueDevices().isEmpty());
    now += 1s;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::normalIntervalMayExceedBackoffCeiling()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 10min});

    QCOMPARE(scheduler.intervalFor(QStringLiteral("SERIAL-A")), 10min);
    now += 5min;
    QVERIFY(scheduler.dueDevices().isEmpty());
    now += 5min;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::backoffGrowthUsesExactJitterAndSuccessResetsIt()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay + 10ms; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});
    now += 1s;
    (void)scheduler.dueDevices();

    scheduler.recordResult(QStringLiteral("SERIAL-A"), false);
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 2010ms);
    now += 2010ms;
    (void)scheduler.dueDevices();
    scheduler.recordResult(QStringLiteral("SERIAL-A"), false);
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 4010ms);
    now += 4010ms;
    (void)scheduler.dueDevices();
    scheduler.recordResult(QStringLiteral("SERIAL-A"), true);
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 1s);
}

void PollSchedulerTest::schedulingNearClockMaximumDoesNotOverflow()
{
    PollScheduler::TimePoint now = PollScheduler::TimePoint::max() - 500ms;
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});

    QVERIFY(scheduler.dueDevices().isEmpty());
    now = PollScheduler::TimePoint::max();
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::removesObsoleteInterestsAndLastDeviceSchedule()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget-a"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power"), QStringLiteral("app")}, 1s});
    scheduler.setInterest({QStringLiteral("automation-a"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power"), QStringLiteral("audio")}, 5s});

    QVERIFY(scheduler.removeInterest(QStringLiteral("widget-a"), QStringLiteral("SERIAL-A")));
    QCOMPARE(scheduler.deviceCount(), 1);
    QCOMPARE(scheduler.intervalFor(QStringLiteral("SERIAL-A")), 5s);
    QCOMPARE(scheduler.fieldsFor(QStringLiteral("SERIAL-A")),
             QSet<QString>({QStringLiteral("power"), QStringLiteral("audio")}));
    QVERIFY(!scheduler.removeInterest(QStringLiteral("widget-a"), QStringLiteral("SERIAL-A")));

    QVERIFY(scheduler.removeInterest(QStringLiteral("automation-a"), QStringLiteral("SERIAL-A")));
    QCOMPARE(scheduler.deviceCount(), 0);
}

void PollSchedulerTest::repeatedHealthyInterestDoesNotPostponeDeadline()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    const PollInterest interest{QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                                {QStringLiteral("power")}, 1s};
    scheduler.setInterest(interest);

    for (int registration = 0; registration < 9; ++registration) {
        now += 100ms;
        scheduler.setInterest(interest);
        QVERIFY(scheduler.dueDevices().isEmpty());
    }
    now += 100ms;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::removeAndReaddPreservesActiveClaim()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    const PollInterest interest{QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                                {QStringLiteral("power")}, 100ms};
    scheduler.setInterest(interest);
    now += 100ms;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));

    QVERIFY(scheduler.removeInterest(QStringLiteral("widget"), QStringLiteral("SERIAL-A")));
    scheduler.setInterest(interest);
    now += 100ms;
    QVERIFY(scheduler.dueDevices().isEmpty());

    scheduler.recordResult(QStringLiteral("SERIAL-A"), true);
    now += 100ms;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

void PollSchedulerTest::schedulingAtClockMinimumDoesNotOverflow()
{
    PollScheduler::TimePoint now = PollScheduler::TimePoint::min();
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});

    now += 999ms;
    QVERIFY(scheduler.dueDevices().isEmpty());
    now += 1ms;
    QCOMPARE(scheduler.dueDevices(), QStringList({QStringLiteral("SERIAL-A")}));
}

QTEST_GUILESS_MAIN(PollSchedulerTest)
#include "poll_scheduler_test.moc"
