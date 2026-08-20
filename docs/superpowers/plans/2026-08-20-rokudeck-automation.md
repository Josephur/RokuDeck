# RokuDeck Audio Automation and Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Linux-audio-driven Roku rules with fresh-state conditions, deterministic reconciliation, manual-override protection, crash recovery, and the global volume ceiling.

**Architecture:** A PulseAudio-compatible observer emits typed playback/output snapshots. A pure rule engine evaluates triggers and conditions; a command planner reconciles active intents and applies device-wide safety invariants before the daemon dispatches anything.

**Tech Stack:** C++20, Qt 6 Core/DBus/Test, libpulse, KF6 Config/Notifications, D-Bus, QML/Kirigami

**Spec:** `docs/superpowers/specs/2026-08-20-rokudeck-v1-design.md`

## Global Constraints

- Complete the foundation and Plasma widget plans first.
- Observe PipeWire through its PulseAudio-compatible API; do not poll `pactl` or `wpctl` output.
- `PowerOn` includes screensaver mode; screensaver is an independent tri-state condition.
- Unknown or stale TV values do not satisfy positive conditions.
- Never wake a TV unless an explicit user-configured power action requests it.
- All rule, widget, CLI, and restoration paths pass through the same volume-maximum guard.
- Prefix local shell commands with `rtk`.

---

### Task 1: PulseAudio-compatible observer and deterministic fake

**Files:**
- Create: `src/automation/CMakeLists.txt`
- Create: `src/automation/audio_snapshot.h`
- Create: `src/automation/audio_observer.h`
- Create: `src/automation/pulse_audio_observer.h`
- Create: `src/automation/pulse_audio_observer.cpp`
- Create: `tests/support/fake_audio_observer.h`
- Create: `tests/unit/pulse_audio_observer_test.cpp`
- Create: `tests/manual/audio_observer_probe.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: libpulse mainloop callbacks.
- Produces: `AudioSnapshot`, `PlaybackStream`, `AudioObserver::snapshotChanged(const AudioSnapshot &)`, and `PulseAudioObserver::start()`.

- [ ] **Step 1: Write failing normalization tests**

```cpp
void PulseAudioObserverTest::marksSelectedHeadsetActive()
{
    AudioSnapshot snapshot;
    snapshot.defaultSinkName = "alsa_output.usb-SteelSeries_Arctis";
    snapshot.activePortName = "analog-output-headphones";
    snapshot.headsetMatchers = {QRegularExpression("Arctis|headphones")};
    snapshot.normalize();
    QVERIFY(snapshot.selectedHeadsetActive);
}
```

- [ ] **Step 2: Confirm the test fails**

```bash
rtk cmake --build --preset dev
rtk ctest --preset dev -R audio-observer --output-on-failure
```

- [ ] **Step 3: Implement the observer boundary**

```cpp
struct PlaybackStream {
    quint32 index = 0;
    QString applicationName;
    QString mediaName;
    QString sinkName;
    bool corked = true;
};

struct AudioSnapshot {
    QString defaultSinkName;
    QString activePortName;
    QList<PlaybackStream> streams;
    QList<QRegularExpression> headsetMatchers;
    bool selectedHeadsetActive = false;
    QDateTime capturedAt;
    void normalize();
};
```

Integrate libpulse file descriptors and timers into the Qt event loop. Subscribe to sink, sink-input, server, card, and default-device changes. Coalesce callback bursts into one snapshot emission.

- [ ] **Step 4: Test against the deterministic fake and live audio inventory**

```bash
rtk ctest --preset dev -R audio-observer --output-on-failure
rtk build/dev/tests/audio-observer-probe --json
```

Expected: tests pass and the live command reports the active default output and playback applications without changing audio state.

- [ ] **Step 5: Commit**

```bash
rtk git add src/automation tests
rtk git commit -m "feat(automation): observe Linux audio state"
```

### Task 2: Rule schema and fresh-state condition evaluator

**Files:**
- Create: `src/automation/rule.h`
- Create: `src/automation/rule.cpp`
- Create: `src/automation/rule_json.h`
- Create: `src/automation/rule_json.cpp`
- Create: `src/automation/condition_evaluator.h`
- Create: `src/automation/condition_evaluator.cpp`
- Create: `tests/unit/condition_evaluator_test.cpp`

**Interfaces:**
- Consumes: `AudioSnapshot` and `DeviceSnapshot`.
- Produces: `AutomationRule`, `AudioTrigger`, `RuleCondition`, `RuleAction`, `ConditionResult`, `parseRuleJson`, and `serializeRuleJson`.

- [ ] **Step 1: Write failing power/screensaver independence tests**

```cpp
void ConditionEvaluatorTest::powerOnIncludesScreensaver()
{
    const auto tv = snapshot(PowerState::On, TriState::Active);
    QCOMPARE(evaluate(PowerCondition::On, tv), ConditionResult::Match);
    QCOMPARE(evaluate(ScreensaverCondition::Active, tv), ConditionResult::Match);
}

void ConditionEvaluatorTest::standbyMakesScreensaverUnknown()
{
    const auto tv = snapshot(PowerState::Standby, TriState::Active);
    QCOMPARE(evaluate(ScreensaverCondition::Active, tv), ConditionResult::Unknown);
}
```

- [ ] **Step 2: Confirm failure, then define the exact schema**

```cpp
enum class PowerCondition { Any, On, Standby };
enum class ScreensaverCondition { Any, Active, Inactive };
enum class MediaCondition { Any, Playing, Paused, Stopped, Idle };
enum class ReachabilityCondition { Any, Reachable, Unreachable };

enum class AudioTriggerKind {
    PlaybackStarted,
    PlaybackStopped,
    DefaultOutputChanged,
    HeadsetActivated,
    HeadsetDeactivated
};

struct AudioTrigger {
    AudioTriggerKind kind;
    QStringList applicationMatchers;
    QStringList outputMatchers;
};

struct ForegroundCondition {
    enum class Target { Any, Home, App, Input } target = Target::Any;
    QString value;
};
struct MuteCondition { std::optional<bool> expected; };
struct VolumeCondition { enum class Operator { Less, LessOrEqual, Equal, GreaterOrEqual, Greater } op; int value; };
using ConditionValue = std::variant<PowerCondition, ScreensaverCondition,
                                    ForegroundCondition, MediaCondition,
                                    MuteCondition, VolumeCondition,
                                    ReachabilityCondition>;
struct RuleCondition { ConditionValue value; };

struct RuleAction {
    enum class Kind { Mute, Unmute, SetVolume, AdjustVolume, PowerOn, PowerOff, Notify } kind;
    std::optional<int> numericValue;
    QString message;
};

struct AutomationRule {
    QUuid id;
    QString name;
    bool enabled = false;
    QStringList targetSerials;
    AudioTrigger trigger;
    QList<RuleCondition> conditions;
    QList<RuleAction> actions;
    std::chrono::milliseconds onsetDebounce{1500};
    std::chrono::milliseconds restoreDelay{3000};
};
```

JSON must include `schemaVersion: 1`. Reject unknown required enum values and preserve unknown optional object keys when rewriting rules so additive upgrades do not destroy data.

- [ ] **Step 3: Implement Match-all evaluation**

Return `NoMatch` for a definite false predicate and `Unknown` when required fresh data is absent. A rule is eligible only when every condition returns `Match`. Support power, screensaver, foreground type/app/input, media, mute, numeric volume comparison, and reachability.

```cpp
ConditionResult ConditionEvaluator::evaluateAll(const QList<RuleCondition> &conditions,
                                                const DeviceSnapshot &tv) const
{
    bool sawUnknown = false;
    for (const auto &condition : conditions) {
        const auto result = evaluate(condition, tv);
        if (result == ConditionResult::NoMatch) return result;
        sawUnknown |= result == ConditionResult::Unknown;
    }
    return sawUnknown ? ConditionResult::Unknown : ConditionResult::Match;
}
```

- [ ] **Step 4: Run tests and commit**

```bash
rtk ctest --preset dev -R condition-evaluator --output-on-failure
rtk git add src/automation tests/unit
rtk git commit -m "feat(automation): add versioned rule conditions"
```

### Task 3: Rule state machine and overlapping-intent reconciler

**Files:**
- Create: `src/automation/rule_engine.h`
- Create: `src/automation/rule_engine.cpp`
- Create: `src/automation/intent_reconciler.h`
- Create: `src/automation/intent_reconciler.cpp`
- Create: `tests/unit/rule_engine_test.cpp`
- Create: `tests/unit/intent_reconciler_test.cpp`

**Interfaces:**
- Consumes: parsed rules, audio snapshots, and fresh device snapshots.
- Produces: `RulePhase { Idle, TriggerPending, Active, RestorePending }`, `RuleDecision`, `DeviceIntent`, and `IntentReconciler::reconcile`.

- [ ] **Step 1: Write failing debounce and overlap tests**

```cpp
void RuleEngineTest::waitsForOnsetDebounce()
{
    engine.setRule(ruleWithDebounce(1500ms));
    engine.update(audioPlaying(), tvOn(), at(0ms));
    QCOMPARE(engine.phase(), RulePhase::TriggerPending);
    engine.tick(at(1499ms));
    QCOMPARE(engine.phase(), RulePhase::TriggerPending);
    engine.tick(at(1500ms));
    QCOMPARE(engine.phase(), RulePhase::Active);
}

void IntentReconcilerTest::muteWinsAndLowestVolumeWins()
{
    const auto result = reconcile({muteIntent(), volumeIntent(25), volumeIntent(15)});
    QCOMPARE(result.muted, true);
    QCOMPARE(result.volume, 15);
}
```

- [ ] **Step 2: Confirm tests fail, then implement the pure state machine**

Use an injected monotonic clock. Evaluate conditions throughout the active trigger. If a TV changes to standby, clear ownership without producing a restore command. Record decisions `Activated`, `Suppressed`, `Applied`, `RestoreScheduled`, `Restored`, and `Relinquished` with machine-readable reasons.

```cpp
if (snapshot.power == PowerState::Standby && state.phase == RulePhase::Active) {
    ledger.relinquish(rule.id, snapshot.serial, "tv-entered-standby");
    state.phase = RulePhase::Idle;
    return RuleDecision::Relinquished;
}
```

- [ ] **Step 3: Implement deterministic reconciliation**

Group intents by serial and property. Mute dominates numeric volume. Among numeric targets choose the minimum. Keep the set of owning rule IDs so restoration waits until no active owner remains.

```cpp
DeviceIntent IntentReconciler::reconcile(const QList<DeviceIntent> &intents)
{
    DeviceIntent result;
    result.muted = std::ranges::any_of(intents, &DeviceIntent::requestsMute);
    for (const auto &intent : intents) {
        if (intent.volume) result.volume = std::min(result.volume.value_or(*intent.volume), *intent.volume);
        result.ownerRuleIds.unite(intent.ownerRuleIds);
    }
    return result;
}
```

- [ ] **Step 4: Run tests and commit**

```bash
rtk ctest --preset dev -R 'rule-engine|intent-reconciler' --output-on-failure
rtk git add src/automation tests/unit
rtk git commit -m "feat(automation): reconcile safe audio rules"
```

### Task 4: Action ledger, manual override, and device-wide volume guard

**Files:**
- Create: `src/automation/action_ledger.h`
- Create: `src/automation/action_ledger.cpp`
- Create: `src/automation/volume_guard.h`
- Create: `src/automation/volume_guard.cpp`
- Create: `src/automation/command_planner.h`
- Create: `src/automation/command_planner.cpp`
- Create: `tests/unit/action_ledger_test.cpp`
- Create: `tests/unit/volume_guard_test.cpp`
- Modify: `src/plasmoid/qml/config/ConfigDevices.qml`

**Interfaces:**
- Consumes: reconciled device intents, fresh device snapshots, and per-device `VolumePolicy`.
- Produces: `PlannedCommand`, `GuardResult { QList<PlannedCommand> commands; std::optional<SafetyRejection> rejection; }`, `SafetyRejection`, `ActionLedger::recover`, and `VolumeGuard::apply`.

- [ ] **Step 1: Write failing safety tests**

```cpp
void VolumeGuardTest::clampsRuleAndRestoreTargets()
{
    VolumePolicy policy{.enabled = true, .maximum = 20};
    const auto rule = guard.apply(setVolume(80), snapshotAtVolume(10), policy);
    const auto restore = guard.apply(restoreVolume(45), snapshotAtVolume(15), policy);
    QCOMPARE(rule.commands.front().volume, 20);
    QCOMPARE(restore.commands.front().volume, 20);
}

void VolumeGuardTest::lowersBeforeUnmuteAboveMaximum()
{
    VolumePolicy policy{.enabled = true, .maximum = 20};
    const auto plan = guard.apply(unmute(), snapshotAtVolume(45, true), policy);
    QCOMPARE(plan.commands, QList({setVolume(20), unmute()}));
}
```

- [ ] **Step 2: Confirm failure, then implement the invariant**

The guard must cover automation, widget, CLI, and restoration sources. Reject volume-up and unmute when current volume is unknown. Permit mute and downward commands. Enabling the setting never emits a command; `lowerToMaximumNow(serial)` is a separate explicit operation.

```cpp
GuardResult VolumeGuard::apply(const PlannedCommand &command,
                               const DeviceSnapshot &snapshot,
                               const VolumePolicy &policy) const
{
    if (!policy.enabled) return {{command}, std::nullopt};
    if (!snapshot.volume && command.canIncreaseLoudness()) {
        return {{}, SafetyRejection::UnknownCurrentVolume};
    }
    return clampAndOrder(command, snapshot, policy.maximum);
}
```

Add the Devices-page controls only in this step: **Enable volume maximum**, the validated numeric maximum, current-volume comparison text, and **Lower to maximum now**. The UI calls daemon policy methods and never performs clamping itself.

- [ ] **Step 3: Implement persisted ownership and crash recovery**

Persist original value, last applied value, property, owning rule IDs, and timestamp in `rokudeckrc`. On restart, obtain fresh trigger and TV state. Restore only when no trigger owns the property and current TV value equals `lastApplied`; otherwise relinquish and log the reason.

```cpp
if (entry.ownerRuleIds.isEmpty() && currentValue == entry.lastAppliedValue) {
    return PlannedCommand::restore(entry.property, entry.originalValue);
}
ledger.relinquish(entry.key(), "manual-change-or-active-owner");
return std::nullopt;
```

- [ ] **Step 4: Run tests and commit**

```bash
rtk ctest --preset dev -R 'action-ledger|volume-guard' --output-on-failure
rtk git add src/automation tests/unit
rtk git commit -m "feat(safety): enforce volume ceiling and recovery"
```

### Task 5: Daemon, CLI, and Plasma automation integration

**Files:**
- Create: `src/daemon/automation_controller.h`
- Create: `src/daemon/automation_controller.cpp`
- Create: `src/plasmoid/qml/config/ConfigAutomations.qml`
- Create: `src/plasmoid/qml/config/RuleEditor.qml`
- Create: `src/plasmoid/qml/config/ConditionRow.qml`
- Create: `src/plasmoid/qml/config/ActionRow.qml`
- Create: `tests/integration/automation_dbus_test.cpp`
- Create: `tests/qml/tst_automation_editor.qml`
- Modify: `data/dbus/io.github.josephur.RokuDeck1.xml`
- Modify: `src/cli/command_line.cpp`
- Modify: `src/plasmoid/qml/config/config.qml`

**Interfaces:**
- Consumes: rule engine, command planner, daemon device state, and audio observer.
- Produces: D-Bus rule CRUD, dry-run, test, pause, activity methods; matching CLI commands; approved rule editor.

- [ ] **Step 1: Write failing D-Bus dry-run integration test**

```cpp
void AutomationDbusTest::dryRunRecordsDecisionWithoutTvCommand()
{
    daemon.setAudioSnapshot(audioPlaying("Firefox"));
    daemon.setDeviceSnapshot(tvOnAtVolume(45));
    call("TestRule", {ruleJson(), true});
    QCOMPARE(fakeRunner.commandCount(), 0);
    QVERIFY(activity().contains("Dry run"));
}
```

- [ ] **Step 2: Extend D-Bus and CLI contracts**

Add `ListRules`, `PutRule`, `DeleteRule`, `SetRuleEnabled`, `TestRule`, `PauseAutomations`, `GetActivity`, and `GetAudioSnapshot`. CLI forms are `rules`, `rule get`, `rule put`, `rule enable`, `rule disable`, `rule test --dry-run`, `automations pause DURATION`, and `activity`.

```xml
<method name="ListRules"><arg direction="out" type="s" name="rulesJson"/></method>
<method name="PutRule"><arg direction="in" type="s" name="ruleJson"/><arg direction="out" type="s" name="ruleId"/></method>
<method name="TestRule"><arg direction="in" type="s" name="ruleJson"/><arg direction="in" type="b" name="dryRun"/><arg direction="out" type="s" name="decisionJson"/></method>
<method name="PauseAutomations"><arg direction="in" type="u" name="durationSeconds"/></method>
<method name="GetActivity"><arg direction="out" type="s" name="activityJson"/></method>
```

- [ ] **Step 3: Implement the approved editor**

The editor shows When, If, Then, and Restore sections; onset and restore delays; retry and notification settings; dry run and test actions; and live decision preview. Power and screensaver are separate conditions. `Only run while TV is on` writes `PowerCondition::On` and does not add a screensaver condition.

```qml
ConditionRow {
    label: i18n("TV power")
    choices: [i18n("Either"), i18n("On"), i18n("Off or standby")]
}
ConditionRow {
    label: i18n("Screensaver")
    choices: [i18n("Either"), i18n("Active"), i18n("Inactive")]
}
```

- [ ] **Step 4: Run automated tests and live, human-assisted checks**

```bash
rtk ctest --preset dev --output-on-failure
rtk build/dev/src/cli/rokudeckctl rule test --dry-run tests/fixtures/rules/firefox-duck.json
```

Then ask the user to validate one physical behavior at a time: Home, screensaver, standby suppression, Firefox playback, headset output, manual TV-volume override, overlapping rules, and maximum-volume clamp. Never combine state changes when one observation is ambiguous.

- [ ] **Step 5: Commit and push**

```bash
rtk git add src/daemon src/cli src/plasmoid data/dbus tests
rtk git commit -m "feat: add first-class Roku audio automation"
rtk git push origin main
```
