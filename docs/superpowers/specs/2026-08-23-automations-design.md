# RokuDeck Automations — Design

- Status: Approved direction, awaiting implementation plan
- Date: 2026-08-23
- Owner: daemon (`rokudeckd`); widgets are clients
- References studied (ideas only, no code copied): Home Assistant automation/script engine
  (trigger → condition → action, execution modes), Easer (event/condition/profile separation)

## 1. Goals

1. Users create If-Then rules once; they run for every widget and for the CLI without any
   Plasma session running.
2. Rules are robust and extensible: new trigger, condition, and action kinds can be added
   without schema breaks.
3. Safety stays centralized: every action dispatches through the same validated command path
   the widget uses, gated by per-device consent and a volume ceiling.
4. Non-technical users can read a rule as a plain-language sentence.

Non-goals for v1: visual flow/graph editing, scripting expressions (QJS), cloud sync,
per-widget rule visibility filters.

## 2. Ownership and consent

- Rules live in the daemon's config store, shared by all clients. Widgets hold **zero** rule
  state; they render and edit through D-Bus.
- Every action targeting a device checks that device's existing `allowAutomations` flag at
  dispatch time. A rule naming only non-consenting devices reports `consent-blocked` in its
  last-run record instead of firing silently.
- The per-device **volume ceiling** is an always-on policy, not a rule: absolute `setVolume`
  from any source (automation action, future UI slider) clamps to `[0, ceiling]`. Absent
  ceiling = unclamped. Relative ECP keys (`VolumeUp`/`VolumeDown`) cannot be clamped by the
  protocol and stay exempt — the ceiling therefore governs automations and absolute setters,
  not remote-style nudging. Changes take effect on the next volume command; no retroactive
  adjustment.

## 3. Data model

All structures serialize as JSON with `"schemaVersion": 1`, matching existing envelope style.

```jsonc
Rule {
  "id": "r7q…",              // daemon-generated opaque id
  "name": "Night volume",
  "enabled": true,
  "devices": ["X000…"] | "*",
  "mode": "queued",           // single | restart | queued (default queued)
  "trigger": Trigger,
  "conditions": CondTree?,    // optional extra gate evaluated at fire time
  "actions": ActionList       // 1..n
}

Trigger :=
  { "type": "time",     "kind": "daily"|"weekdays"|"once", "time": "HH:MM",
    "date": "YYYY-MM-DD"? }                       // date only for "once"
| { "type": "interval", "minutes": N }            // repeating cadence, min 5
| { "type": "stateChange", "field": Field, "op": Op, "value": V }
                                                  // edge-triggered on transitions
| { "type": "manual" }

Field := power | screensaver | app | muted | volume | health
Op    := is | isNot | gt | lt | between

CondTree :=
  { "all": [CondTree…] } | { "any": [CondTree…] } | { "not": CondTree }
| { "leaf": { "device": serial|"*", "field": Field, "op": Op, "value": V } }

Action :=
  { "type": "keypress", "key": "Home" }
| { "type": "launchApp", "appId": "tv.netflix…" }
| { "type": "setVolume", "volume": 0..100 }        // clamped by ceiling
| { "type": "mute", "muted": true|false }
| { "type": "delay", "seconds": 1..600 }
| { "type": "runRule", "id": "r7q…" }              // macros; cycles detected
| { "type": "ifThenElse",
    "cond": CondTree,
    "then": ActionList,
    "else": ActionList? }                          // recursive like HA's script blocks
```

Validation lives daemon-side in one place (mirrors `parseSnapshot` discipline): unknown
types/ops/fields are rejected with a specific error string; `between` requires a two-value
array; `delay` bounds enforced; recursion depth of `CondTree`/`ActionList` capped (16).

## 4. Engine architecture

The evaluator is a daemon-internal object fed by three sources:

1. **Snapshot diffs** — the poll pipeline already produces fresh snapshots; the engine keeps
   the previous snapshot per device and emits state-change events on field transitions
   (`old != new`). Only fields present in both snapshots participate; first observation
   after startup establishes baseline without firing.
2. **Timer wheel** — one QTimer per pending schedule, re-anchored in local time after each
   fire and after system wake (re-check on every snapshot tick too). `once` triggers whose
   time passed while the daemon was down fire once at startup if less than 24h stale;
   otherwise skipped as stale.
3. **D-Bus `RunRule`** — manual/macros from widgets or CLI.

Dispatch pipeline per firing:

```
mode gate (see §5) → conditions (CondTree against latest snapshots)
  → for each action sequentially:
      delay → sleep within worker
      runRule → recurse with cycle set (depth cap 16, then error event)
      device action → resolve target(s) → allowAutomations check
        → SendCommand internal path (same validation/safety as widgets)
        → record outcome per device
```

Failures never abort sibling actions of an unrelated device; an action failing for one
device continues the list for others. Each rule run appends a bounded last-run record
(`lastFiredAt`, `outcome`: ok | partial | failed | consent-blocked | condition-false).

### Mode semantics (from HA practice)

- `single`: drop the firing while a previous run is active.
- `restart`: cancel the active run, start fresh.
- `queued` (default): run after the active one finishes (bounded queue depth 8; overflow
  drops oldest and records `dropped`).

## 5. Storage

`ConfigStore` gains a `[rules]` parent group mirroring `[devices]`:

```ini
[rules]
[rules][<id>]
Name=Night volume
Enabled=true
Definition={"schemaVersion":1,…}   ;; full Rule JSON minus id/name/enabled
```

Ordering is insertion order via group order, like devices. Writes go through the existing
config-store transaction path so concurrent widget edits serialize. Deleting a device leaves
rule device lists untouched: dispatch skips serials the daemon no longer knows and records
`partial`, and the editor shows a warning badge on rules referencing missing devices.

## 6. D-Bus additions to `io.github.josephur.RokuDeck1`

```xml
<method name="ListRules">   <arg direction="out" type="s" name="rulesJson"/> </method>
<method name="AddRule">     <arg direction="in" type="s" name="ruleJson"/>
                            <arg direction="out" type="s" name="ruleJson"/> </method>
<method name="UpdateRule">  <arg direction="in" type="s" name="ruleJson"/> </method>
<method name="DeleteRule">  <arg direction="in" type="s" name="ruleId"/> </method>
<method name="SetRuleEnabled"> <arg direction="in" type="s" name="ruleId"/>
                               <arg direction="in" type="b" name="enabled"/> </method>
<method name="RunRule">     <arg direction="in" type="s" name="ruleId"/> </method>
<signal name="RulesChanged"> <arg type="s" name="rulesJson"/> </signal>
```

Plus `VolumeCeiling` (optional int) added to `UpdateDevice`'s changes JSON and echoed in
device JSON. Errors reuse the existing annotation style
(`io.github.josephur.RokuDeck1.Error.InvalidRule`, `.UnknownRule`, `.RuleCycle`).

## 7. Widget UI

Settings gains an **Automations** page (after Controls):

- List: rows show enable switch, name, plain-language summary, last-run outcome chip.
- Summary renderer maps the JSON to sentences: “Weekdays at 23:00 · When volume > 20 ·
  Set Living Room TV volume to 15”; unknown future types degrade to “Custom rule”.
- Editor sections: When (trigger), Extra conditions, Then, Optional Else. Structured
  pickers only in v1 — no free text. Device pickers list consenting devices by default.
- Flyout unchanged; manual rules may surface there later.

Client plumbing mirrors `DevicesModel`: an `AutomationsModel` over `ListRules` +
`RulesChanged`, with edit calls returning server-normalized JSON (single write authority =
daemon).

## 8. Testing strategy

- Engine: headless QtTest suite with fake clock + fake process runner (patterns exist in
  `FakeProcessRunner`) covering each trigger kind, mode semantics, consent gating, ceiling
  clamping, recursion caps, missed-schedule policy, and diff baseline behavior.
- Storage: round-trip + concurrent-write serialization tests alongside existing config-store
  tests.
- D-Bus: extend the fake-daemon harness (`plasmoid_bridge_test`) for CRUD + `RulesChanged`
  delivery; integration test exercises the real daemon service end-to-end.
- QML: Automations page CRUD + summary rendering via the fixture bridge, following
  `tst_configuration.qml` patterns.

## 9. Milestones

- M1 Model, validation, storage, D-Bus CRUD + signals (engine stubbed: manual rules only).
- M2 Time + interval scheduler with missed-fire policy.
- M3 State-change triggers, condition trees, ifThenElse actions.
- M4 Volume ceiling policy end-to-end (store → clamp → settings toggle).
- M5 Automations settings page: list, summaries, structured editor.

Each milestone ships green behind the full test suite; M1–M2 already deliver user-visible
value (scheduled power-off macros).
