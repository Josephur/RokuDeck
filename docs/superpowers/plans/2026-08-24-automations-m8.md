# Automations M8 Implementation Plan — Rule Flow Extensions

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development adapted to milestone-executor mode (one executor owns all tasks; controller reviews the branch).

**Goal:** Rules read like the user's sentences: stateChange triggers can watch a specific device, any trigger can be restricted to a time-of-day window, and rules can run user-approved local scripts by name.

**Architecture:** Three additive grammar extensions — optional `device` on stateChange triggers (evaluator-side scoping), optional `activeWindow` block on every trigger (fire-time gate producing a new `inactive-window` outcome), and a `runScript` action executing files from the daemon's scripts directory through the existing queued/delivery machinery via a `program` override.

**Tech Stack:** C++20, Qt 6 Core/Test, QML/JS editor, QtTest.

**Spec:** `docs/superpowers/specs/2026-08-24-rule-flow-extensions-design.md` (binding)

## Global Constraints

- Standard repo constraints (literals, `-Werror`, suite green per task, commit convention, out-of-class test bodies).
- New outcome literal: `inactive-window`.
- Script name regex: `^[a-z0-9][a-z0-9_-]{0,63}$`.
- LOCAL-PC opt-in ruling is untouched and must keep passing.

## Rulings

1. Window evaluation happens in `requestRuleFire` BEFORE the mode gate; outside ⇒ targeted lastRun writeback outcome `inactive-window` + announceRules + return (mirrors the condition-false pattern). Applies to all three origins.
2. Window boundary: inclusive start, exclusive end; `end < start` wraps midnight; `start == end` rejected at parse ("degenerate window").
3. Scripts directory = `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scripts"` (resolved once). The draft spec's `~/.local/share/rokudeckd/scripts` wording yields to this standard-paths location; CHANGELOG documents it.
4. Script execution: canonical path must stay inside the canonical directory; file must exist, be a regular file, and pass `QFile::setPermissions`-check executable (`.isExecutable()`); otherwise ++rejected. Enqueued as `QueuedCommand{program=absolutePath, arguments={}, trackDelivery=true}` with new `CommandContract::scriptTimeoutMs = 30000`; exit 0 ⇒ delivered.
5. `runScript` cannot re-enter rules: no call-stack interaction.

## Tasks

### Task 1: stateChange device key
- Parser (`src/core/rule_model.cpp` stateChange branch): accept `{type,field,op,value}` or `{type,device,field,op,value}`; device = non-empty string ≤64 chars, default "*"; stored into the predicate's device field.
- Evaluator (`src/core/rule_conditions.cpp` `dueStateChangeRuleIds`): skip rules whose `trigger.stateChange.device` is non-wildcard and ≠ arriving serial. LOCAL-PC opt-in daemon-side filter unchanged.
- Tests: parser both shapes + bad device types rejected; evaluator: rule scoped to SERIAL-B ignores SERIAL-A edges but fires on B; wildcard behavior identical to before.
- Commit `feat(core): device-scoped state change triggers`.

### Task 2: activeWindow
- Parser: every trigger type accepts an optional `"activeWindow": {"start":"HH:MM","end":"HH:MM"}` key (both variants of each exact-key set); valid HH:MM; reject start==end ("degenerate window"); store `std::optional<TimeWindow>` on TriggerSpec (struct in `rule_model.h`: two QStrings or QTimes — implementer's choice, document).
- Daemon: in `requestRuleFire`, after enabled checks and before mode gate: outside-window ⇒ targeted writeback outcome `inactive-window` + announceRules + return (all origins; RunRule slot returns "inactive-window").
- Summary renderer: append `· active <start>–<end>` when present (golden test).
- Editor (Task 4 wires UI): payload includes/excludes the block.
- Tests: parser shapes/rejections; wrap-midnight window fires at 23:00 and 07:00 but not 12:00 (fake wall clock via existing patterns); inactive-window persisted + announced; boundary inclusivity (fires at exactly start, not at exactly end).
- Commit `feat(daemon): time of day windows for automation triggers`.

### Task 3: runScript
- Parser: allowedTypes += runScript; exact keys {type,script}; name regex per constraints; ActionNode gains `script`.
- `CommandContract` += `scriptTimeoutMs = 30000`.
- Dispatch: executableTypes += runScript; branch resolves directory (QStandardPaths::AppDataLocation + "/scripts", computed once), validates canonical containment + exists + isFile + isExecutable ⇒ enqueue program-routed command (scriptTimeoutMs); else ++rejected.
- `processNextCommand`: non-rds programs skip rds flag composition and use scriptTimeoutMs.
- Tests: golden parse; integration with temp AppDataLocation? QStandardPaths is process-global — use existing config-dir harness: create scripts dir under the test's writable location via `QStandardPaths::setTestModeEnabled`?? Simplest deterministic: inject the scripts dir as another defaulted DaemonService ctor param (QString scriptsDir = {}) defaulting to the standard path — tests pass a temp dir. Pin: happy run (exit 0 ⇒ ok), exit 1 ⇒ failed, missing/non-executable/path-escape ⇒ failed zero traffic, name regex rejection at AddRule.
- Commit `feat(daemon): runScript action for user-approved local scripts`.

### Task 4: Editor wiring
- When section: device picker for stateChange (Any + `bridge.deviceEntries()`), writing `trigger.device` (omitted when Any).
- Active section: Anytime radio / Between + two HH:MM fields → `activeWindow`.
- Actions menu: Run script step (text field, client regex validation).
- `rulepayload.js`: buildPayload/toFormState/validate cover all three (goldens extended in tst_rulepayload.qml: device-scoped trigger, window round-trip, runScript payload, window degenerate rejection).
- Commit `feat(plasmoid): editor support for device scopes, windows, and scripts`.

### Task 5: Hygiene
- CHANGELOG bullets (three features, honest script-security wording incl. actual resolved directory). Full suite. Final commit `docs(changelog): rule flow extensions`.
- No install-dev — controller deploys.

## Self-review checklist
Spec §1→T1 (+editor T4), §2→T2 (+T4), §3→T3 (+T4), §4 deferred untouched. Opt-in ruling pins keep passing. All new literals (`inactive-window`, regexes, timeout) pinned by tests.
