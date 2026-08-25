# Rule Flow Extensions — Design Addendum ("rules that read like sentences")

- Status: Draft for approval (extends `2026-08-23-automations-design.md` + PC addendum)
- Date: 2026-08-24
- Motivation: a user's natural rule reads "Devices: This PC + Living Room TV · WHEN the PC
  starts playing audio · between 22:00–08:00 · extra: TV must be ON · Then mute the TV".
  Four grammar gaps stand between that sentence and today's model.

## 1. stateChange triggers gain a device

Today every stateChange trigger observes ALL devices (`device` is hardwired `"*"`).
The trigger grammar gains an optional `"device"` key:

```jsonc
{ "type": "stateChange", "device": "LOCAL-PC", "field": "playing", "op": "is", "value": true }
```

- Absent ⇒ `"*"` (any device) — every existing rule keeps its meaning.
- Present ⇒ edges are evaluated against that device's snapshots only.
- The opt-in ruling still applies on top: wildcard never observes LOCAL-PC; explicit
  `LOCAL-PC` does.
- Editor: the When section's stateChange branch grows a device picker (Any + known devices),
  defaulting to Any.

## 2. Time-of-day windows on any trigger

Any trigger gains an optional block restricting WHEN it may fire:

```jsonc
{ "trigger": { …,
  "activeWindow": { "start": "22:00", "end": "08:00" } } }
```

- `start == end` means always-active is rejected (useful window must be non-degenerate);
  `end < start` wraps midnight (22:00→08:00 spans overnight).
- Evaluated at fire time against the local wall clock, BEFORE the mode gate. Outside the
  window the firing is recorded with a new outcome: `inactive-window` (joins the §4 enum;
  editor chip renders it grey like consent-blocked).
- Works uniformly across time/interval/stateChange/manual triggers.
- Editor: an "Active" section — Anytime (default) or Between two time pickers.

## 3. `runScript` action

```jsonc
{ "type": "runScript", "script": "movie-night" }
```

- Executes `<dataDir>/rokudeckd/scripts/<name>` (the daemon's own data dir, e.g.
  `~/.local/share/rokudeckd/scripts/`) directly — `ProcessRunner`, no shell, no arguments,
  bounded timeout, exit 0 ⇒ delivered.
- Security shape: `script` must match `[a-z0-9][a-z0-9_-]{0,63}`; the resolved canonical
  path must remain inside the scripts directory; the file must exist and be executable at
  fire time. Anything else counts as one rejected attempt (`failed`/`partial` aggregation
  unchanged). Rules reference scripts BY NAME — raw command strings never cross D-Bus.
- Delivery verdicts fold into the existing outcome machinery (ok/partial/failed).
- Editor: a script-name text field with the same validation regex (the widget cannot list
  daemon-local files; the daemon stays authoritative).
- Recursion cap: scripts run OUTSIDE the rule call-stack concept (they cannot re-enter rules);
  depth accounting unaffected.

## 4. Explicitly deferred

Follow-up pairing ("also create the unmute twin") is editor UX sugar over two rules — noted
for a future editor release; it needs no grammar change.

## Testing

Parser goldens for each new shape + rejection cases; evaluator tests for device-scoped edges
and window boundaries inclusive/exclusive at endpoints; integration tests driving fake wpctl/
MPRIS plus a real temp scripts directory (executable, non-executable, escaping path rejected);
editor picker assertions ride existing QML coverage.
