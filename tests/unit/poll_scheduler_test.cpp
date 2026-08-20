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
    const QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.front().serial, QStringLiteral("SERIAL-A"));
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
    const QList<PollClaim> initialClaims = scheduler.claimDuePolls();
    QCOMPARE(initialClaims.size(), 2);
    for (const PollClaim &claim : initialClaims) {
        QVERIFY(scheduler.recordResult(claim, claim.serial == QStringLiteral("SERIAL-HEALTHY")));
    }
    now += 100ms;

    const QList<PollClaim> due = scheduler.claimDuePolls();
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.front().serial, QStringLiteral("SERIAL-HEALTHY"));
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
        now += scheduler.delayFor(QStringLiteral("SERIAL-A"));
        const QList<PollClaim> claims = scheduler.claimDuePolls();
        QCOMPARE(claims.size(), 1);
        QVERIFY(scheduler.recordResult(claims.front(), false));
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

    const QList<PollClaim> firstClaims = scheduler.claimDuePolls();
    QCOMPARE(firstClaims.size(), 1);
    QCOMPARE(firstClaims.front().serial, QStringLiteral("SERIAL-A"));
    QVERIFY(scheduler.claimDuePolls().isEmpty());
    QVERIFY(scheduler.recordResult(firstClaims.front(), true));
    now += 100ms;
    const QList<PollClaim> secondClaims = scheduler.claimDuePolls();
    QCOMPARE(secondClaims.size(), 1);
    QCOMPARE(secondClaims.front().serial, QStringLiteral("SERIAL-A"));
    QVERIFY(secondClaims.front().token != firstClaims.front().token);
}

void PollSchedulerTest::repeatedInterestPreservesDeadlineAndBackoff()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    const PollInterest interest{QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                                {QStringLiteral("power")}, 1s};
    scheduler.setInterest(interest);
    now += 1s;
    const QList<PollClaim> initialClaims = scheduler.claimDuePolls();
    QCOMPARE(initialClaims.size(), 1);
    QVERIFY(scheduler.recordResult(initialClaims.front(), false));
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 2s);

    scheduler.setInterest(interest);
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 2s);
    now += 1s;
    QVERIFY(scheduler.claimDuePolls().isEmpty());
    now += 1s;
    const QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.front().serial, QStringLiteral("SERIAL-A"));
}

void PollSchedulerTest::normalIntervalMayExceedBackoffCeiling()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 10min});

    QCOMPARE(scheduler.intervalFor(QStringLiteral("SERIAL-A")), 10min);
    now += 5min;
    QVERIFY(scheduler.claimDuePolls().isEmpty());
    now += 5min;
    const QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.front().serial, QStringLiteral("SERIAL-A"));
}

void PollSchedulerTest::backoffGrowthUsesExactJitterAndSuccessResetsIt()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay + 10ms; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});
    now += 1s;
    QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);

    QVERIFY(scheduler.recordResult(claims.front(), false));
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 2010ms);
    now += 2010ms;
    claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QVERIFY(scheduler.recordResult(claims.front(), false));
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 4010ms);
    now += 4010ms;
    claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QVERIFY(scheduler.recordResult(claims.front(), true));
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), 1s);
}

void PollSchedulerTest::schedulingNearClockMaximumDoesNotOverflow()
{
    PollScheduler::TimePoint now = PollScheduler::TimePoint::max() - 500ms;
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});

    QVERIFY(scheduler.claimDuePolls().isEmpty());
    now = PollScheduler::TimePoint::max();
    const QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.front().serial, QStringLiteral("SERIAL-A"));
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
        QVERIFY(scheduler.claimDuePolls().isEmpty());
    }
    now += 100ms;
    const QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.front().serial, QStringLiteral("SERIAL-A"));
}

void PollSchedulerTest::removeAndReaddPreservesActiveClaim()
{
    PollScheduler::TimePoint now{};
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    const PollInterest interest{QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                                {QStringLiteral("power")}, 100ms};
    scheduler.setInterest(interest);
    now += 100ms;
    const QList<PollClaim> originalClaims = scheduler.claimDuePolls();
    QCOMPARE(originalClaims.size(), 1);
    const PollClaim originalClaim = originalClaims.front();

    QVERIFY(scheduler.removeInterest(QStringLiteral("widget"), QStringLiteral("SERIAL-A")));
    scheduler.setInterest(interest);
    now += 100ms;
    QVERIFY(scheduler.claimDuePolls().isEmpty());

    QVERIFY(scheduler.recordResult(originalClaim, true));
    now += 100ms;
    const QList<PollClaim> newerClaims = scheduler.claimDuePolls();
    QCOMPARE(newerClaims.size(), 1);
    const PollClaim newerClaim = newerClaims.front();
    QVERIFY(newerClaim.token != originalClaim.token);

    const auto deadlineDelay = scheduler.delayFor(QStringLiteral("SERIAL-A"));
    const auto newerDeadline = scheduler.nextDueFor(QStringLiteral("SERIAL-A"));
    QVERIFY(newerDeadline.has_value());
    QVERIFY(!scheduler.recordResult(originalClaim, false));
    QCOMPARE(scheduler.delayFor(QStringLiteral("SERIAL-A")), deadlineDelay);
    QVERIFY(scheduler.nextDueFor(QStringLiteral("SERIAL-A")) == newerDeadline);
    QVERIFY(scheduler.claimDuePolls().isEmpty());

    QVERIFY(scheduler.recordResult(newerClaim, true));
    now += 100ms;
    const QList<PollClaim> finalClaims = scheduler.claimDuePolls();
    QCOMPARE(finalClaims.size(), 1);
    QVERIFY(finalClaims.front().token != newerClaim.token);
}

void PollSchedulerTest::schedulingAtClockMinimumDoesNotOverflow()
{
    PollScheduler::TimePoint now = PollScheduler::TimePoint::min();
    PollScheduler scheduler([&] { return now; }, [](auto delay) { return delay; });
    scheduler.setInterest({QStringLiteral("widget"), QStringLiteral("SERIAL-A"),
                           {QStringLiteral("power")}, 1s});

    now += 999ms;
    QVERIFY(scheduler.claimDuePolls().isEmpty());
    now += 1ms;
    const QList<PollClaim> claims = scheduler.claimDuePolls();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.front().serial, QStringLiteral("SERIAL-A"));
}

QTEST_GUILESS_MAIN(PollSchedulerTest)
#include "poll_scheduler_test.moc"
