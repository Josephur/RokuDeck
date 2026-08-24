# PC State Source — Design Addendum

- Status: Approved direction (extends `2026-08-23-automations-design.md`)
- Date: 2026-08-24
- Owner: daemon (`rokudeckd`); widgets are clients
- Motivation: rules should react to the machine they run on — "when the PC starts
  playing audio, mute the TV", "when PC volume rises above X".

## 1. Model: This PC as a synthetic device

The daemon auto-creates one reserved device at startup:

```ini
[Devices][N]
Serial=LOCAL-PC
DisplayName=This PC
AllowAutomations=true   ;; forced; UpdateDevice rejects false with "the local device always allows automations"
```

- Serial literal `LOCAL-PC`; display name defaults to "This PC" and IS user-renameable
  (nameIsUserChosen machinery works unchanged).
- Undeletable (`RemoveDevice` on LOCAL-PC replies InvalidArgs "the local device cannot be removed")
  and never produced by discovery.
- Consent is always-on: it is the machine the daemon runs on.

## 2. State sources

### 2.1 Audio poller (PipeWire via wpctl)

Every 2 s: `wpctl get-volume @DEFAULT_AUDIO_SINK@` → parse `Volume: 0.58` (optionally suffixed
`[MUTED]`) into a synthetic snapshot: `volume = round(0.58 * 100)`, `muted`, `power = On`,
`health = Online`. Three consecutive failures flip health to `Offline` until a success.
Snapshots flow through the existing pipeline: `m_snapshots` insert, state-change diffs,
`DeviceSnapshotChanged`.

### 2.2 MPRIS watcher (event-driven, zero binaries)

Watches the session bus for `org.mpris.MediaPlayer2.*` services; tracks each service's
`PlaybackStatus` (initial Get on appearance + `PropertiesChanged` subscriptions). The synthetic
field `playing` is true iff ANY tracked player reports `Playing`. Services appearing/vanishing
recompute immediately.

## 3. Grammar extension: the `playing` field

§3's Field enum gains `playing` (bool). Accepted in stateChange triggers and condition leaves
(ops `is`/`isNot` only — numeric ops on a bool never match, mirroring `muted`). Summaries render
"When playing is true/false"; the editor field picker lists it with a Mute-style switch input.
Roku snapshots simply never carry `playing` (absent ⇒ never matches, per the existing rule).

## 4. Actions against This PC

`setVolume`/`mute` targeting LOCAL-PC execute exactly via PipeWire:
`wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.58` / `wpctl set-mute @DEFAULT_AUDIO_SINK@ 1|0`.
No nudge-walk, no observed-volume gating — absolute control is real here. The M5 ceiling still
clamps the target. `keypress`/`launchApp` aimed at LOCAL-PC count as rejected attempts
(`unsupportedCommand`). Mechanically, queued commands gain a `program` field (default `rds`);
PC commands run `wpctl` without the rds-specific flags.

## 5. Lifecycle

- An **enabled** rule whose devices reference LOCAL-PC (or wildcard) keeps the daemon alive,
  exactly like scheduled rules; otherwise the daemon may idle-exit as before. The poller itself
  runs whenever the daemon runs (2 s wpctl is cheap).
- The widget's device list shows This PC like any device; its detail page hides the Roku remote
  controls (nothing to press) but keeps rename + volume ceiling.

## 6. Testing

FakeProcessRunner covers wpctl parsing/failure paths and action dispatch shapes; a fake MPRIS
service drives the watcher in integration tests; config-store tests pin auto-creation and
undeletability; editor field-picker coverage rides the existing golden/QML tests plus a picker
assertion.
