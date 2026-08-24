# Automations M6 Implementation Plan — Full §7 Rule Editor

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development adapted to milestone-executor mode (one executor owns all tasks; controller reviews the branch). Steps use checkbox syntax.

**Goal:** Users create and edit automation rules entirely inside the widget's Automations page: a structured editor with pickers for every trigger type, every action type, and arbitrarily nested condition trees — no free-form JSON anywhere.

**Architecture:** Payload assembly is factored into a pure JS module (`qml/rulepayload.js`) that builds/validates the exact rule JSON the daemon parser accepts, unit-pinned via the existing qmltestrunner harness. `ConfigRuleEditor.qml` is a Kirigami dialog opened for add (from a header button) and edit (per-row affordance); it renders structured sections per spec §7 (When / Extra conditions / Then / Optional Else), submits through the existing bridge (`AddRule`/`UpdateRule`), and surfaces server validation errors through the page's existing error banner. The list page gains an Add button and row-edit entry points.

**Tech Stack:** QML (QtQuick/Controls/Kirigami/PlasmaComponents), JS, QtTest qmltestrunner, C++ bridge tests.

**Spec:** `docs/superpowers/specs/2026-08-23-automations-design.md` §3 (exact grammar your payloads must satisfy), §7 (editor structure), §8 (QML test patterns)

## Global Constraints

- Payloads MUST parse under the daemon's strict `parseRule`: exact-key sets per shape, name 1..80 chars, mode in single/restart/queued, interval minutes 5..1440, delay 1..600, volume 0..100, keypress key from the daemon allowlist (Home Back Up Down Left Right Select Play Pause Rev Fwd InstantReplay FindRemote Power PowerOn), appId regex `^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$`, runRule id 1..64 chars `[A-Za-z0-9_-]`-ish (daemon regex `^[A-Za-z0-9_-]+$` on ≤64), conditions exactly one of all/any/not/leaf per node, leaf needs device+field+op+value with field ∈ power|screensaver|app|muted|volume|health and op ∈ is|isNot|gt|lt|between (between ⇒ two-element array value), depth cap 16.
- Editing round-trips: `UpdateRule` takes the COMPLETE document — preserve `id`, `enabled`, and the loaded rule's `lastRun` block verbatim; only the edited fields change.
- House style: QStringLiteral/_s equivalents in QML are plain strings + `KI18n.i18nc`; test bodies outside classes where C++ is touched.
- Full suite green before every commit. Commit convention `type(scope): lowercase imperative`.

## Rulings

1. Value inputs are typed per field: power/screensaver/health → combo of canonical values; muted → switch; volume → 0..100 spin; app → text (validated client-side by the appId regex only for launchApp actions — condition `app` values are free strings compared by op).
2. Device pickers list all known devices plus an explicit "Any device" entry mapping to `"*"`; rules storing specific serials that later disappear keep them (daemon already tolerates unknown serials at dispatch).
3. The editor does NOT live-preview summaries; rows re-render from the server-normalized `RulesChanged` payload after save.
4. runRule target picker lists other rules from `bridge.automations` (excluding the rule being edited); ids stay selectable even if that list is empty (fallback manual id entry) because macros may reference rules created later.
5. No editor-side schemaVersion/id injection for AddRule beyond what the daemon already tolerates (it injects schemaVersion and assigns id/enabled).

## Tasks (self-execute in order, TDD where testable, one commit each)

### Task 1: Pure payload assembly module + golden tests
- Create `src/plasmoid/qml/rulepayload.js` with `.pragma library` exposing:
  - `blankRule()` → minimal editable form-state object;
  - `buildPayload(formState, existingRule)` → JSON string for AddRule (new) or complete UpdateRule document (edit: splice id/enabled/mode/devices/trigger/actions/conditions over existingRule preserving lastRun);
  - `toFormState(ruleJsonObject)` → inverse mapping for editing;
  - `validate(formState)` → list of human-readable problem strings (name empty/too long; time malformed HH:MM; once without date; interval out of range; actions empty; between without two values; appId/key regex failures). Client-side validation is convenience only — the daemon remains authoritative.
- Tests: new file `tests/qml/tst_rulepayload.qml` following existing qmltest patterns in `tests/qml/` (register nothing — the plasmoid-qml test scans the directory). Golden-string cases: full manual+keypress rule; weekdays+setVolume+delay; once+launchApp; interval+mute; stateChange trigger with between condition value; nested all/any/not tree; ifThenElse with then+else; round-trip toFormState→buildPayload equality; validate() rejection cases.
- Commit `feat(plasmoid): rule payload assembly with golden tests`.

### Task 2: Editor dialog shell + trigger section
- Create `src/plasmoid/qml/ConfigRuleEditor.qml` (Kirigami.Dialog with standard footer Save/Cancel): name field, enabled switch (edit only), mode combo, devices multi-select (CheckBoxes from `bridge.devices`, none checked = any device), When section implementing all four trigger types per rulings (stateChange: device picker incl Any, field combo, op combo, typed value editor per ruling 1; `between` swaps in two numeric fields).
- Wire open-for-add from `ConfigAutomations.qml` header (PlasmaExtras/ToolButton "Add rule", objectName "addRuleButton") and open-for-edit per row (click on the row body, objectName "ruleEditArea"); Save calls `bridge.addRule(payload)` or `bridge.updateRule(payload)`; server errors surface through the page's existing operationError banner connection; close on successful `deviceOperationSucceeded("addRule"/"updateRule")`.
- Register in `src/plasmoid/CMakeLists.txt` QML_SOURCES.
- Commit `feat(plasmoid): structured rule editor shell with triggers`.

### Task 3: Conditions builder
- Extend ConfigRuleEditor: "Extra conditions" section rendering a recursive node editor (component `ConditionNodeEditor` inline or separate file): group kind combo (All/Any/Not/Leaf) → All/Any render child repeaters with add-child/add-group/remove controls; Not renders one child slot; Leaf renders device/field/op/value editors (same typed-value pattern as stateChange). Depth display indent; hard-cap nesting input at depth 12 (parser allows 16; headroom for groups).
- Round-trips through rulepayload.js (`conditions` present only when non-empty).
- Commit `feat(plasmoid): nested condition builder for rules`.

### Task 4: Actions builder
- Then/Else sections (Then required non-empty, Else optional): ordered action-list editor with add-step menu of the seven types and per-type editors — keypress (key combo from the daemon allowlist), launchApp (text + regex validation), setVolume (0..100 slider), mute (true/false combo rendered Mute/Unmute), delay (1..600 s spin), runRule (picker per ruling 4), ifThenElse (inline recursive cond mini-builder + Then/Else sub-lists using the same components as Tasks 3–4 — recursion via Component delegation, depth-capped at 8 levels of action nesting).
- Reordering: up/down buttons per step (objectNames "actionMoveUp"/"actionMoveDown").
- Commit `feat(plasmoid): structured action list editor`.

### Task 5: Integration polish + hygiene
- Bridge-level C++ test additions in `tests/unit/plasmoid_bridge_test.cpp`: end-to-end editor flow simulation — build payload via the same JSON shapes (embed golden payloads from Task 1), AddRule through FakeDaemon (extend it with AddRule/UpdateRule/ListRules slots mirroring real semantics minimally), assert RulesChanged-driven model row appears with correct summary role, UpdateRule rename persists across proxy recreation.
- Manual-verification checklist appended to the report (open settings → add each rule family → confirm rows/summaries/chips) since visual behavior can't be asserted here.
- CHANGELOG bullet: "The Automations page gains a full structured editor — create and edit every trigger, condition, and action type without touching a terminal."
- Full suite; final commit `docs(changelog): automations rule editor`.
- Do NOT run scripts/install-dev.sh — controller deploys after review.

## Self-review checklist for the executor
Every §3 grammar constraint enforced either client-side (validate) or delegated to the daemon with errors surfaced; §7 sections present (When/Extra conditions/Then/Optional Else); summary rendering untouched (server-normalized); no placeholders — component names, objectNames, and behaviors above are the contract; where Kirigami API details are unspecified, mirror ConfigDevices.qml/DeviceDetail.qml patterns exactly.
