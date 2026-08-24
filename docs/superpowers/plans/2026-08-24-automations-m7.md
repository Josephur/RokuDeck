# Automations M7 Implementation Plan — PC State Source

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development adapted to milestone-executor mode (one executor owns all tasks; controller reviews the branch).

**Goal:** The daemon detects and acts on its own machine: a reserved "This PC" device whose PipeWire volume/mute and MPRIS playing-state feed the existing trigger/condition/action engine — including exact `setVolume`/`mute` actions on the PC.

**Architecture:** A reserved device (`LOCAL-PC`) is bootstrapped into the registry/config and guarded against deletion/consent-off. Two sources feed synthetic snapshots through the unchanged state-change pipeline: a 2 s `wpctl get-volume` poller (ProcessRunner pattern) and an event-driven MPRIS watcher (pure D-Bus). Queued commands gain a `program` field so PC dispatches run `wpctl` instead of `rds`. The rule grammar gains one bool field, `playing`.

**Tech Stack:** C++20, Qt 6 Core/DBus/Test, KConfig, QtTest, PipeWire CLI (`wpctl`), MPRIS2 D-Bus.

**Spec:** `docs/superpowers/specs/2026-08-24-pc-state-source-design.md` (binding addendum)

## Global Constraints

- `QStringLiteral(...)` / `_s`; `-Werror`; suite green per task; commit convention; out-of-class test bodies.
- Reserved serial literal: `LOCAL-PC`. Default display name: `This PC`.
- Outcome strings unchanged; `playing` ops restricted to `is`/`isNot` (numeric ops never match a bool, mirroring `muted`).

## Tasks

### Task 1: Grammar — the `playing` field
- Modify: `src/core/device_state.h` (`std::optional<bool> playing;` on DeviceSnapshot), `src/core/rule_model.cpp` (allowed fields += `playing`; value must be bool for it — reject strings/numbers like `muted`), `src/core/rule_conditions.cpp` (render true/false; absent ⇒ never matches), `src/core/rule_summary.cpp` (stateChange rendering works generically once conditions render — verify "When playing is true").
- Modify: `src/daemon/daemon_service.cpp` `snapshotJson` emits `"playing"` (null when unset).
- Test: extend `tests/unit/rule_model_test.cpp`, `rule_conditions_test.cpp`, `rule_summary_test.cpp`: parser accepts `{"type":"stateChange","field":"playing","op":"is","value":true}`; numeric op on playing rejected ("never match" semantics live in evaluator — parser may accept then evaluator never matches; pin evaluator behavior); summary golden "When playing is true · Press Home".
- Commit `feat(core): playing field for media playback state`.

### Task 2: LOCAL-PC bootstrap + guards
- Modify: `src/daemon/daemon_service.cpp` ctor: after loading devices, if none has serial LOCAL-PC, upsert `{serial:LOCAL-PC, displayName:This PC, address:"127.0.0.1", sortIndex:nextSortIndex()}` and persist; `RemoveDevice`: LOCAL-PC ⇒ InvalidArgs "the local device cannot be removed"; `validateDeviceJson`/UpdateDevice path: `allowAutomations:false` targeting LOCAL-PC ⇒ InvalidArgs "the local device always allows automations"; rename allowed.
- Idle parity per spec §5: private helper `bool hasEnabledLocalPcRules() const` (any enabled rule explicitly listing LOCAL-PC in devices); `hasAcceptedWork()` returns true when it does.
- Test: fresh temp-config daemon owns This PC (ListDevices); persists across recreateDaemon; RemoveDevice/consent-off rejected with exact messages; explicit-reference enabled rule blocks idle (`idleTimerActive()==false`), disabling releases.
- Commit `feat(daemon): reserved local pc device`.

### Task 3: Audio poller
- Modify: daemon_service.{h,cpp}: `QTimer m_pcPollTimer` (2 s) → ProcessRunner `wpctl get-volume @DEFAULT_AUDIO_SINK@` → parse `Volume: <0..1>` optional `[MUTED]` suffix → synthetic DeviceSnapshot{serial LOCAL-PC, volume=round(v*100), muted, power On, health Online} inserted via the SAME path as Roku polls (hadPrevious/stateChange diff/DeviceSnapshotChanged emit). Three consecutive failures ⇒ health Offline snapshot; success restores.
- Test: FakeProcessRunner completes wpctl requests; volume 18→5 edge fires a stateChange rule on LOCAL-PC; `[MUTED]` parses muted=true; 3 failures ⇒ health offline in GetSnapshot; recovery restores online.
- Commit `feat(daemon): pipewire audio poller for the local pc`.

### Task 4: MPRIS watcher
- Modify: daemon_service.{h,cpp}: session-bus watcher tracking `org.mpris.MediaPlayer2.*` names; on appear query `PlaybackStatus`, subscribe `PropertiesChanged`; `playing = any(Playing)` merged into subsequent LOCAL-PC snapshots (poller merges this flag each tick).
- Test (bridge-style): fake MPRIS QDBusContext service registers/appears → Get PlaybackStatus Playing → next poller tick's snapshot carries playing=true; status change to Paused ⇒ false; service vanishes ⇒ false. State-change rule on `playing` fires across a transition.
- Commit `feat(daemon): mpris watcher for local playback state`.

### Task 5: Exact PC actions
- Modify: `QueuedCommand` gains `QString program = QStringLiteral("rds");`; `dispatchActionList`: targets on LOCAL-PC — setVolume builds `wpctl set-volume @DEFAULT_AUDIO_SINK@ <v/100 2-decimals>` (M5 ceiling clamps target first; delta==0 no-op; missing snapshot volume impossible while poller healthy — treat stale-health as ++rejected), mute builds `wpctl set-mute @DEFAULT_AUDIO_SINK@ 1|0` (already-equal ⇒ no-op); keypress/launchApp targeting LOCAL-PC ⇒ ++rejected; `processNextCommand` uses `command.program`, skipping rds-specific flags (`--ip/--json/--quiet/--timeout` composition stays rds-only).
- Test: integration — setVolume 58 on LOCAL-PC ⇒ runner request program==wpctl arguments exactly {set-volume,@DEFAULT_AUDIO_SINK@,0.58}; ceiling clamps; mute toggle shapes; keypress-on-PC rule ⇒ failed zero traffic.
- Commit `feat(daemon): exact volume and mute actions on the local pc`.

### Task 6: Widget + hygiene
- DeviceDetail.qml: hide the remote-controls section when `serial === "LOCAL-PC"` (rename + ceiling remain).
- Editor field pickers (ConfigRuleEditor/RuleValueInput field combos): add `playing` mapping to the bool input.
- Bridge/QML test touch: fixture device list includes LOCAL-PC variant assertion (detail-page gating via model data, light).
- CHANGELOG bullets; full suite; final commit `docs(changelog): this pc automations`.
- No install-dev — controller deploys.

## Self-review checklist
Spec §1–§6 each map to T1–T6; reserved-serial literals identical everywhere (`LOCAL-PC`); playing semantics identical between parser/evaluator/summary/editor; wpctl argument shapes pinned by tests; idle-parity behavior pinned.
