# Automations M3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rules become real automations: `stateChange` triggers fire on snapshot transitions, condition trees gate any firing, and `setVolume`/`mute`/`runRule`/`ifThenElse` actions execute (removing the M1 `unsupportedAction` stubs).

**Architecture:** A headless evaluator in `src/core/rule_conditions.{h,cpp}` answers three questions over snapshots: does a predicate match, is a condition tree satisfied, and which stateChange rules flip false→true between two snapshots. `DaemonService` feeds it at the poll-completion hook, dispatches firings through the existing guarded pipeline, and extends action execution with relative-key volume/mute expansion, macro recursion with call-stack cycle detection, and recursive ifThenElse branches.

**Tech Stack:** C++20 (no exceptions, `QT_NO_CAST_FROM_ASCII`), Qt 6 Core/DBus/Test, KConfig, QtTest.

**Spec:** `docs/superpowers/specs/2026-08-23-automations-design.md` (§2 consent/ceiling, §3 data model + ops semantics, §4 dispatch pipeline/outcomes, §8 testing)

## Global Constraints

- All string literals: `QStringLiteral(...)` or `_s` (tests: `using namespace Qt::Literals::StringLiterals;`). ASCII casts are compile errors.
- Build is `-Werror`; no exceptions in daemon/core targets.
- Every task ends green under the full suite (`ctest` from `build/dev`) before committing.
- Commit messages follow repo convention: `type(scope): lowercase imperative summary`.
- House style: slot/test bodies are defined OUTSIDE the class (this toolchain's GCC rejects in-class redefinition after declaration).
- Outcome strings stay within spec §4: `ok | partial | failed | consent-blocked | condition-false | dropped | cycle-error`.

## M3 Rulings (plan decisions under the approved spec)

1. **No absolute ECP volume exists.** `rds` offers only relative `VolumeUp`/`VolumeDown`/`VolumeMute` keypresses. `setVolume` executes as a bounded relative walk from the latest observed snapshot volume (`min(|delta|, 30)` presses, assuming ~1 unit per press), then triggers a fresh poll. Missing/unknown snapshot volume ⇒ the attempt is rejected (counted as a failure) rather than steering blind. Spec §2 already frames protocol-limited volume mechanics as best-effort. M5's ceiling work can refine this.
2. **`mute` is a conditional toggle:** if the latest snapshot's `muted` already equals the target, the step is a successful no-op; otherwise one `VolumeMute` press. Unknown `muted` state ⇒ rejected attempt.
3. **stateChange uses rising edge only** (predicate false→true fires; true→false does not — invert the predicate for that). First observation after startup is baseline and never fires (spec §4.1). `old == new` never re-fires (spec §3).
4. **Condition leaves with `device:"*"` require ALL of the rule's current targets to match** (predictable all-targets gate); a serial leaf requires that device's latest snapshot to match; missing snapshot ⇒ no match. A predicate whose field value is absent/unknown never matches, including for `isNot`.
5. **runRule macros recurse with a call stack:** re-entering an id already on the stack aborts only that branch, counts as a failure, and forces the *current* run's outcome to `cycle-error`. Child outcomes aggregate into the parent (`ok` child = success unit; anything else = one failure). A missing child rule id counts as one rejection.
6. **Mode semantics stay deferred to M4** (consistent with prior ruling); M3 keeps sequential execution with the existing `m_dispatchActive` guard.
7. **stateChange firings run the full pipeline** — consent gate, conditions gate, delivery-aware outcomes — through the same `executeRuleRun` path as manual/scheduled runs.

## File Structure

- Create: `src/core/rule_conditions.{h,cpp}` — pure predicate/tree/state-change evaluation against `DeviceSnapshot`.
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` (new `rule-conditions-test` target).
- Create: `tests/unit/rule_conditions_test.cpp`.
- Modify: `src/daemon/daemon_service.{h,cpp}` — poll hook, guarded state-change dispatch, condition gate, new action execution, call-stack plumbing.
- Test: Modify `tests/integration/daemon_dbus_test.cpp` (poll-driving pattern from `addressChangeInvalidatesSnapshotAndRejectsStalePoll`, lines ~534-600).

---

### Task 1: Core condition evaluator

**Files:**
- Create: `src/core/rule_conditions.h`, `src/core/rule_conditions.cpp`
- Modify: `src/core/CMakeLists.txt` (append `rule_conditions.cpp`)
- Test: Create `tests/unit/rule_conditions_test.cpp`; register `rule-conditions-test` after `rule-summary-test` (links `RokuDeck::Core Qt6::Core Qt6::Test`, include `${PROJECT_SOURCE_DIR}/src`)

**Interfaces:**
- Consumes: `FieldPredicate`, `ConditionNode`, `RuleDocument` from `core/rule_model.h`; `DeviceSnapshot` from `core/device_state.h`.
- Produces (Tasks 2–6):
  ```cpp
  namespace RokuDeck {
  // Field value rendering/lookup lives inside; absent or Unknown values never
  // match, including for isNot.
  [[nodiscard]] bool predicateMatches(const FieldPredicate& predicate,
                                      const QHash<QString, DeviceSnapshot>& snapshots,
                                      const QStringList& targets);
  [[nodiscard]] bool conditionsMet(const ConditionNode& conditions,
                                   const QHash<QString, DeviceSnapshot>& snapshots,
                                   const QStringList& targets);
  // Enabled stateChange rules whose predicate flips false->true between
  // previous and current, in rule-list order.
  [[nodiscard]] QStringList dueStateChangeRuleIds(const QList<RuleDocument>& rules,
                                                  const QString& serial,
                                                  const DeviceSnapshot& previous,
                                                  const DeviceSnapshot& current);
  }
  ```

- [ ] **Step 1: Write the failing tests**

Cover at minimum (data-driven where natural):
- `power` is/isNot/isNot-missing (`is "on"` matches On; unknown snapshot never matches, even `isNot`);
- `volume` gt/lt/between closed interval (`between [10,20]` matches 10 and 20);
- `muted` is true/false; numeric op on bool never matches;
- `app` is matches `activeApp->id`; no activeApp never matches;
- `health` is;
- device scoping: named serial present+matching vs absent serial never matches;
- tree walks: `all`/`any`/`not` nesting;
- `dueStateChangeRuleIds`: rising edge fires; falling edge does not; stable does not; disabled rule skipped; manual/time rules ignored; rule-list order preserved.

Fixtures build `DeviceSnapshot` directly (aggregate: power/volume/muted/activeApp/health fields) and rules via `parseRule(QJsonObject{...})` exactly like `tests/unit/rule_scheduler_test.cpp`.

- [ ] **Step 2: Register target, verify RED** — `cmake --build build/dev --target rule-conditions-test` fails: header missing.

- [ ] **Step 3: Implement**

Field extraction: `power` → `on|standby|unknown` via enum switch; `screensaver` → `active|inactive|unknown`; `health` → `online|standby|degraded|offline|unknown`; `muted` → only if optional present, render `true/false`; `volume` → only if optional present; `app` → `snapshot.activeApp->id` only when present. Ops: `is`/`isNot` compare rendered string (numeric fields compare canonical decimal rendering); `gt`/`lt` strict numeric; `between` CLOSED `[low,high]`; non-numeric operands for numeric ops never match. Predicate `device` selects whose snapshot: `"*"` = every entry in `targets` must match (empty targets ⇒ no match); otherwise that serial's snapshot must exist and match. Tree walk per `ConditionNode::Kind` (All=AND over children, Any=OR, Not=inverts single `notChild` front entry, Leaf=predicate with `leaf.device`). `dueStateChangeRuleIds` = filter enabled + `trigger.type=="stateChange"`, evaluate trigger predicate against `previous` then `current`, keep ids where `!prevMatch && currMatch`.

- [ ] **Step 4: GREEN** — `ctest -R rule-conditions` passes.
- [ ] **Step 5: Full suite once**, commit `feat(core): automation condition evaluation against snapshots`.

---

### Task 2: stateChange trigger engine in the daemon

**Files:**
- Modify: `src/daemon/daemon_service.h`, `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Interfaces:**
- Consumes: Task 1's `dueStateChangeRuleIds`; `executeRuleRun` (existing).
- Produces: private `void runStateChangeFirings(const QStringList& ruleIds);` (same `m_dispatchActive` guard pattern as `evaluateDueRules`); public test seam NOT required — tests drive real polls.

- [ ] **Step 1: Failing integration test** — copy the poll-driving pattern from `addressChangeInvalidatesSnapshotAndRejectsStalePoll` (RegisterClient → complete version/device-info/active-app/player/audio-device responses): first full poll (volume 18) establishes baseline; add rule `{"trigger":{"type":"stateChange","field":"volume","op":"lt","value":20},"actions":[keypress Home]}` AFTER baseline; second poll with `<volume>5</volume>` → fake runner sees the keypress and `lastRun.outcome == "ok"`; a third identical poll (volume stays 5) adds NO second request. Also assert a falling-edge variant does not fire (predicate `lt 20`, volume 5 → back to 18: no fire).
- [ ] **Step 2: RED** — runner never sees keypresses.
- [ ] **Step 3: Implement** — in the `pollDueDevices` completion lambda (`daemon_service.cpp:1479` region): capture `const DeviceSnapshot previous = m_snapshots.value(claim.serial);` BEFORE the existing `m_snapshots.insert(...)`, then after the insert + `DeviceSnapshotChanged` emit call:
  ```cpp
  const QStringList due = dueStateChangeRuleIds(m_rules, claim.serial, previous, snapshot);
  runStateChangeFirings(due);
  ```
  `previous` on first observation is a default `DeviceSnapshot`: skip evaluation entirely when the serial was absent from `m_snapshots` before the insert (`hadPrevious` guard) — spec §4.1 baseline without firing. `runStateChangeFirings` mirrors `evaluateDueRules`: guard on `m_dispatchActive`, loop `executeRuleRun(id)`, tail-safe. Note: `executeRuleRun`'s tail `evaluateDueRules()` already exists; do not double-dispatch.
- [ ] **Step 4: GREEN + full suite**, commit `feat(daemon): fire stateChange triggers off snapshot diffs`.

---

### Task 3: Condition gating in the dispatch pipeline

**Files:**
- Modify: `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

- [ ] **Step 1: Failing tests**: manual rule with `conditions` leaf `volume lt 20` against seeded snapshot volume 18 → RunRule returns `ok` and runs; same rule with `gt 50` → outcome string `condition-false`, no runner requests, lastRun records it. (Seed snapshots via the poll pattern helper extracted into a harness helper `seedDeviceSnapshot(serial, volumeXml…)` if repetition gets heavy — reuse literal blocks from the reference test.)
- [ ] **Step 2: RED** — currently conditions ignored → `ok`.
- [ ] **Step 3: Implement** — in `executeRuleRun`, after target resolution and before the empty-targets check... precise placement: resolve consenting `targets` first (existing code, keeps `consent-blocked` precedence), then:
  ```cpp
  if (rule.conditions &&
      !conditionsMet(*rule.conditions, m_snapshots, targets)) {
      rule.lastRun.firedAtIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
      rule.lastRun.outcome = QStringLiteral("condition-false");
      *findRule(rule.id) = rule;
      announceRules();
      evaluateDueRules();
      return QStringLiteral("condition-false");
  }
  ```
- [ ] **Step 4: GREEN + suite**, commit `feat(daemon): gate automation runs on condition trees`.

---

### Task 4: setVolume and mute actions

**Files:**
- Modify: `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Rulings 1–2 apply.** Implementation inside the restructured action dispatcher (see below): extract the per-action body into
```cpp
bool dispatchActionList(const QList<ActionNode>& actions, const QStringList& targets,
                        const QStringList& callStack, int& dispatched, int& rejected,
                        int& deliveryFailures, QStringList& pendingRequests);
```
returning `false` only for `unsupportedAction` (list now only needs no marker — remove the deferred set entirely). `executeActions` becomes: init counters → `dispatchActionList(...)` → existing delivery wait → aggregate `ActionResult`.

Action handling additions (each iterates `targets`, reading `m_snapshots.value(serial)`):
- `mute`: snapshot.muted missing ⇒ `++rejected`; equal ⇒ no-op success; else enqueue one validated `keypress VolumeMute`.
- `setVolume`: snapshot.volume missing ⇒ `++rejected`; `delta==0` ⇒ no-op success; else `min(|delta|,30)` presses of validated `VolumeUp`/`VolumeDown`; enqueue each.

- [ ] **Step 1: Failing tests** (poll-seeded snapshots): `mute` with snapshot muted=false → one VolumeMute request, ok; `mute` already muted → zero requests beyond polls, ok; `setVolume 15` from volume 18 → three VolumeDown presses; `setVolume` with no observed volume (never polled / audio absent) → outcome `failed`; mixed one-device-each mute/setVolume-failure → `partial`.
- [ ] **Step 2: RED** — outcomes are `failed` today (unsupportedAction abort).
- [ ] **Step 3: Implement** as above (remove `deferredTypes`).
- [ ] **Step 4: GREEN + suite**, commit `feat(daemon): execute mute and setVolume actions best-effort`.

---

### Task 5: runRule macros with cycle detection

**Files:**
- Modify: `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Interfaces:** `executeRuleRun(const QString& ruleId, const QStringList& callStack = {})` — callStack carries ancestor ids; pass `callStack + rule.id` into `dispatchActionList` recursion.

Semantics per ruling 5: pre-call check `callStack.contains(action.ruleId)` ⇒ `++deliveryFailures`, set local `cycleDetected=true`, skip branch; missing child id ⇒ `++rejected`; else synchronous nested run: `const QString childOutcome = executeRuleRun(childId, callStack + rule.id);` — `childOutcome == "ok"` ⇒ nothing; otherwise `++deliveryFailures`. After aggregation in `executeRuleRun`: if `result.cycleDetected` ⇒ outcome forced `cycle-error` (before the attempts mapping; consent-blocked still wins as today). Add `bool cycleDetected = false;` to `ActionResult`.

- [ ] **Step 1: Failing tests**: parent with `runRule` child (child = simple keypress rule added first so both consent) → parent outcome `ok`, child ran (two requests total); self-referencing rule (UpdateRule payload with `runRule` pointing at its own id) → outcome `cycle-error`, no infinite loop; missing child id → outcome `failed`.
- [ ] **Step 2: RED** — currently `failed:unsupportedAction`.
- [ ] **Step 3: Implement** as above.
- [ ] **Step 4: GREEN + suite**, commit `feat(daemon): runRule macros with runtime cycle detection`.

---

### Task 6: ifThenElse actions

**Files:**
- Modify: `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

- [ ] **Step 1: Failing tests** (poll-seeded): cond true → only `then` actions dispatched; cond false → only `else`; absent `else` + false cond → clean ok no-op; branch containing `delay` still ordered.
- [ ] **Step 2: RED.**
- [ ] **Step 3: Implement** inside `dispatchActionList`:
  ```cpp
  if (action.type == QStringLiteral("ifThenElse")) {
      const bool taken =
          conditionsMet(action.condition, m_snapshots, targets);
      if (!dispatchActionList(taken ? action.thenActions : action.elseActions,
                              targets, callStack, dispatched, rejected,
                              deliveryFailures, pendingRequests)) {
          return false;
      }
      continue;
  }
  ```
- [ ] **Step 4: GREEN + suite**, commit `feat(daemon): execute ifThenElse branches recursively`.

---

### Task 7: CHANGELOG and hygiene

- [ ] CHANGELOG bullets under `## Unreleased`: stateChange triggers, condition gates (`condition-false`), working `setVolume`/`mute`/`runRule`/`ifThenElse` actions, `cycle-error` detection.
- [ ] Update ledger `.superpowers/sdd/2026-08-24-automations-m3/progress.md` COMPLETE entry.
- [ ] Full suite green; deploy smoke: `bash scripts/install-dev.sh` + create a stateChange rule via busctl while daemon runs (volume drop via TV remote if reachable) or record skip reason.
- [ ] Commit `docs(changelog): automations engine M3`.

## Self-review notes

- Spec coverage: §3 ops/between-closed (T1), §4.1 diff+baseline (T2, ruling 3), §4 pipeline order consent→conditions→actions (T3; mode gate deferred per ruling 6), §4 cycle-error (T5), §3 Action set fully executable after T4–T6, §8 fake-runner patterns throughout.
- Known simplifications surfaced: volume walk assumes ~1 unit/press and does not verify post-walk (M5 refines with ceiling work); `isNot` on absent values never matches (documented T1 behavior); `dropped` outcome remains M4 territory (mode queueing).
- Type consistency: `conditionsMet(predicate-tree signatures)` used verbatim by T2/T3/T6; `dispatchActionList` counters feed the existing `ActionResult` wait phase unchanged; `executeRuleRun` callStack default keeps M2 call sites valid.
