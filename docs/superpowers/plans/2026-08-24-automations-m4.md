# Automations M4 Implementation Plan — Mode Semantics + Engine Polish

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development adapted to milestone-executor mode (one executor owns all tasks; controller reviews the branch). Steps use checkbox syntax. **Execute only after M5 has merged to main** — this plan rebases onto post-M5 `daemon_service.cpp`.

**Goal:** The spec §4 mode gate becomes real: per-rule single/restart/queued semantics govern every firing origin (scheduled, stateChange, D-Bus RunRule), suppressed firings record `dropped`, and the M3-review backlog items (stateChange×conditions integration case, mode-gate-for-manual-runs) close.

**Architecture:** A small per-rule run-state machine lives in DaemonService: `QHash<QString, int> m_runStates` (Idle=0/Active=1/Queued=2) plus a per-active-run cancellation token (`std::shared_ptr<std::atomic_bool>` in `QHash<QString, std::shared_ptr<std::atomic_bool>> m_runTokens`). All three trigger origins route through one new entry point `requestRuleFire(ruleId)` which applies the mode gate; macro recursion (`runRule` actions) calls `executeRuleRun` directly and BYPASSES the gate (an action is not a firing). Cancellation is cooperative: checks between actions and early-exit inside delay waits.

**Tech Stack:** C++20, Qt 6 Core/Test, QtTest.

**Spec:** §4 dispatch pipeline + Mode semantics block (verbatim authority for single/restart/queued/dropped).

## Global Constraints

- Same literal/-Werror/suite-green/commit-convention constraints as all milestones.
- Outcome strings stay within spec §4 (`dropped` joins the recorded set).
- Out-of-class test bodies (toolchain quirk).

## Rulings

1. **The gate applies at firing entries only**: `evaluateDueRules` loop bodies, `runStateChangeFirings` loop bodies, and the `RunRule` D-Bus slot call `requestRuleFire` instead of `executeRuleRun`. Macro children bypass the gate entirely.
2. **single**: firing while Active or Queued ⇒ record `dropped` on that rule (lastRun writeback of ONLY lastRun fields + announceRules, mirroring the M3 targeted-writeback pattern) and do not run.
3. **restart**: firing while Active ⇒ mark the active token cancelled and start fresh immediately (the new run begins once the current dispatchActionList yields; since runs are synchronous, "immediately" means the fresh run executes right after the cancelled one unwinds — implemented by running the fresh run after setting cancellation, sequentially). Firing while Queued ⇒ replace queued with fresh (still one queued slot).
   - A CANCELLED run records NO lastRun and announces nothing — it was superseded; its partial effects already dispatched stay (documented).
4. **queued**: Idle ⇒ run; Active ⇒ occupy the single queued slot; already Queued ⇒ record `dropped`.
5. After a run finishes normally (not cancelled): if state was Queued ⇒ transition to Active and fire again immediately (drain loop), then Idle. Implement as a small loop around executeRuleRun inside requestRuleFire rather than recursion.
6. Delay steps check the token each 100 ms tick and abort the wait early when cancelled; between-action checkpoints skip remaining actions without erroring.
7. The `m_dispatchActive` guard stays exactly as-is (it serializes scheduled/stateChange drains); the mode machine is orthogonal per-rule state. Manual D-Bus runs now ALSO participate via requestRuleFire (closing the M3 review's concurrency note).

## Tasks

### Task 1: Mode machine + cancellation core
- Modify: `src/daemon/daemon_service.{h,cpp}`: members above; private `void requestRuleFire(const QString& ruleId);` `void finishRuleRun(const QString& ruleId, bool cancelled);`; thread a `const std::shared_ptr<std::atomic_bool>& cancelToken` param through `executeActions`/`dispatchActionList` (default nullptr keeps other callers compiling) with between-action checks and delay early-exit; `executeRuleRun` gains token param, returns early (no writeback/announce) when cancelled mid-run.
- Route the three origins through `requestRuleFire`.
- Tests (integration, completer/poll helpers as usual): queued coalesce (two rapid RunRule calls on a delay rule → first ok, second... second arrives while active? D-Bus RunRule is synchronous so a SECOND caller blocks until first finishes — mode gate for manual runs needs CONCURRENT invocations: drive second firing from within the first via evaluateDueRules tail or use two rules? Pin mechanics with scheduled origins instead: two due ticks colliding is impossible (sequential drain)... The honest testable collision: stateChange edge fires rule while its previous delivery-wait still active → second edge during wait → Queued. Test: interval rule minutes=5 fired at T, then force second evaluation while first blocked by un-completable queue? FakeProcessRunner completes everything...
  
  PRACTICAL PIN: unit-style seam — make `requestRuleFire` public (test seam precedent exists) and drive states directly with a rule whose actions include delay 5 (real timer): fire (returns after full run synchronously...) — synchronous D-Bus makes concurrent same-rule arrivals untestable without threads. RESOLUTION (pin in plan): expose internal `_fireModeTest(ruleId, mode-override)`? NO — better: test the state transitions via a public seam `simulateConcurrentFire(ruleId)` = requestRuleFire called re-entrantly during the delay window: schedule QTimer::singleShot(50ms) BEFORE RunRule that calls m_daemon->requestRuleFire(ruleId) again; with mode=single expect dropped recorded; restart → first run's remaining actions skipped (fewer total presses), fresh run completes; queued → third press pattern. These are deterministic because the nested call lands inside the delay event pump.
- Commit `feat(daemon): per-rule execution modes with cooperative cancel`.

### Task 2: dropped recording + precedence tests
- `dropped` path per ruling 2; assert lastRun.outcome=="dropped", no device traffic, RulesChanged announced.
- Precedence: disabled child macros unaffected (gate is trigger-side only — pin with a test that macro invocation of an ACTIVE-running sibling still executes, i.e., no false gating).
- Commit `feat(daemon): record dropped for suppressed firings`.

### Task 3: Backlog closures
- Integration case: stateChange-triggered firing gated by conditions → `condition-false` (seam: poll cycle producing edge on rule with conditions).
- CHANGELOG bullets (modes user-visible wording: "rules gain single/restart/queued execution modes; overloaded firings report dropped").
- Full suite; final commit `docs(changelog): automation execution modes`.
- No install-dev — controller deploys.

## Self-review checklist
Spec §4 verbatim behaviors each have a pinned test (single-drop / restart-supersede / queued-coalesce+drop / drain-after-active / macro-bypass / dropped-recording / cancellation-no-record). Type consistency: requestRuleFire/finishRuleRun names used everywhere. No placeholders.
