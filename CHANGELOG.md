# Changelog

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

All notable changes to RokuDeck are documented in this file.

## Unreleased

- (nothing yet)

## 0.2.0 - 2026-11-14

- The remote panel's mute button flips the moment the daemon accepts the tap
  instead of waiting out the post-command snapshot pull: DaemonProxy predicts
  a mute toggle (keypress `VolumeMute`) from the last reported state and
  reconciles immediately. The confirming snapshot disarms the prediction; a
  failed command reverts the icon at once (with a fresh pull to confirm, so
  the failed toggle cannot linger); a
  capture that raced the command keeps the predicted icon (its fresh volume,
  power, and health still apply); and a capture made well after the
  prediction is believed over it, so a silently rejected command cannot pin
  a wrong state. Rapid taps follow the last tap - the newest prediction
  replaces the armed one and superseded commands can neither revert nor
  regress it.
- Automation settings no longer misreport a rejected change: device toggles
  (automations consent, wake-on-LAN, refresh policy) and the per-rule enable
  switch keep mirroring the daemon's stored value instead of showing an
  unconfirmed edit.
- The rule editor is reliable again: every field, time, date, interval, and
  value edit re-runs validation immediately (Save no longer stays disabled
  after typing the name last); switching a comparison to Between stores a real
  two-number range; a new conditional step opens with an editable condition;
  an inverted condition shows the child you add; and a name/device/window
  round-trip never rewrites the stored values.
- Rules that run local scripts now summarize as "Run script <name>" instead of
  "Custom rule"; a scripted run no longer trips libdbus's 25 s `RunRule` reply
  default; and a mute or keypress queued behind a script waits for it instead
  of being swept as a timeout. The "run another automation" picker lists the
  other rules, and "Rule finished: <outcome>" finally appears after Run now.
- Hardening: a non-array `devices`/`then`/`else` value is now a parse error
  instead of silently widening a rule to every device (or dropping a branch);
  an unparsable stored rule survives later saves instead of being deleted from
  disk; and a pending connection test gets a typed failure when the daemon
  stops.
- Rules that watch one device now say so: `stateChange` triggers accept an optional
  `device` serial (absent keeps the existing any-device meaning), so edges on other devices
  no longer fire the rule — This PC still joins automations only when explicitly named,
  wildcards included.
- Any trigger can be restricted to a time-of-day window via an optional `activeWindow`
  block (`start`/`end` in HH:MM), checked against the local wall clock before each firing —
  inclusive start, exclusive end, `end < start` wraps midnight, and `start == end` is
  rejected as degenerate. Out-of-window firings are recorded and announced with a new
  `inactive-window` outcome across every origin (scheduled, state change, manual). The rule
  editor gains an **Active** section (Anytime / Between two times) and a device picker that
  scopes `stateChange` triggers.
- New `runScript` action executes user-approved local scripts by name from the daemon's own
  scripts directory — `QStandardPaths::AppDataLocation` + `/scripts`, i.e.
  `~/.local/share/rokudeckd/scripts`. The security shape, honestly: rules reference script
  *names* only (`[a-z0-9][a-z0-9_-]{0,63}`; raw command strings never cross D-Bus), and at
  fire time the daemon resolves the canonical path and requires it to remain inside the
  scripts directory (symlink escapes count as failures), executing only regular executable
  files directly — no shell, no arguments, 30-second timeout; missing or non-executable
  files are rejected attempts. The editor adds a Run-script step with the same name check;
  scripts cannot re-enter automations.
- RokuDeck now reacts to the machine it runs on: the daemon auto-creates a reserved
  **This PC** device (`LOCAL-PC`) that cannot be removed and always allows automations
  (rename and volume ceiling stay user-controlled). An enabled rule that explicitly names
  This PC keeps the daemon alive like scheduled rules do; wildcard rules resolve to Roku
  devices alone and never target the PC.
- This PC state flows from the local machine itself: a 2 s PipeWire poll (`wpctl`) feeds
  volume/mute readings (three consecutive failures flip health to `offline`), and an MPRIS
  watcher tracks every media player on the session bus into a new boolean `playing` field.
  Rules can trigger on `playing` and use it in conditions (`is`/`isNot`).
- Automations acting on This PC execute exactly instead of approximately: `setVolume`
  clamps through the volume ceiling then runs `wpctl set-volume` absolutely (no nudge walk),
  and `mute` runs `wpctl set-mute` only when the observed state differs. Keypresses and app
  launches aimed at This PC count as rejected attempts.
- The widget's device detail page hides the remote controls for This PC (nothing to press);
  renaming and the volume ceiling remain available. The rule editor's field picker gains
  `playing` with a Playing/Paused switch.
- Rules gain `single`/`restart`/`queued` execution modes: every trigger origin (schedules,
  state changes, manual runs) passes the rule's mode gate — `single` drops firings that
  arrive while a run is active, `restart` supersedes the active run mid-flight (cooperative
  cancellation, the superseded run records nothing), and `queued` (the default) holds one
  pending firing that drains when the active run finishes. Overloaded firings are recorded
  and announced as `dropped`.
- The Automations page gains a full structured editor — create and edit every trigger,
  condition, and action type without touching a terminal.
- Added a per-device volume ceiling as an always-on automation policy: the daemon stores and
  validates it (0–100), every device JSON surface echoes it, and automation volume actions
  honour the ceiling — absolute `setVolume` targets clamp into range, and relative `VolumeUp`
  walks stop at the latest observed level (best-effort by ECP protocol). Remote-control
  presses are not gated yet.
- The device detail page gains a "Volume ceiling" control with a caption explaining that
  automations never set the volume above it.
- Automation rules can now trigger themselves off device state: `stateChange` triggers fire on
  snapshot transitions (rising edge, baseline-safe), and condition trees (`all`/`any`/`not`) gate
  any firing with a `condition-false` outcome.
- `setVolume`, `mute`, `runRule` macros (with runtime cycle detection → `cycle-error`) and recursive
  `ifThenElse` actions are now executed — `setVolume`/`mute` work through relative volume keys using
  the latest observed state (best-effort by protocol).
- The widget's settings dialog gains an **Automations** page: every daemon-owned rule appears
  live with its plain-language summary, an enable switch, the last-run outcome, and Run-now /
  Delete controls.
- The daemon now fires `time` (daily/weekdays/once) and `interval` automation triggers on
  its own — no widget or CLI session needs to be running, and pending schedules keep the
  daemon alive.
- `once` rules missed while the daemon was down fire once at startup if less than 24 hours
  late; older ones are skipped as stale.
- Added daemon-owned automation rules: a typed JSON rule model with strict validation, persistent
  `[Rules]` storage that survives daemon restarts, and full CRUD over D-Bus (`ListRules`,
  `AddRule`, `UpdateRule`, `DeleteRule`, `SetRuleEnabled`) with `RulesChanged` announcements so
  every client sees edits from anywhere.
- Added manual macro execution via `RunRule`: keypress and app-launch actions dispatch through the
  exact same validated, safety-gated command pipeline as remote controls, honor per-device
  automation consent (devices with automations disabled are never touched), and record a
  last-run outcome on the rule. Scheduled triggers arrive in later milestones.
- Fixed the settings dialog showing empty pages (config QML now lives beside the other UI files),
  and reduced the panel text choice to a simple "Device name" on/off switch.
- Made remote controls fluid again: input re-enables as soon as each command finishes, only the
  pressed control animates, and late out-of-order snapshots can no longer flip mute or volume
  back to stale values.
- Replaced the connection badge with a painted television whose screen color carries device
  state (green online, amber degraded/unknown/stale, red offline, slate standby) in the panel,
  flyout, and desktop views; vertical panels wrap their device name; the tooltip shows the
  selected device's name and status.
- Parsed the real nested `audio-device` XML so volume level and the volume/mute controls actually
  appear, widened `rds` deadlines with process headroom to stop online/degraded flapping on slow
  replies, and logged every failed poll endpoint to the journal.
- The mute button now mirrors the television's current state (struck-through speaker while muted).

- Fixed polling snapshots serving the advertised device name after a user rename, fixed remote
  controls never rendering because no control capabilities were derived, and converted widget
  power/volume/mute dispatch to ECP keypresses (`Power`, `PowerOn`, `VolumeUp`, `VolumeDown`,
  `VolumeMute`) with audio keys gated on an observed volume reading.
- Established the C++20/Qt 6/KF6 build, install, licensing, contribution, security, support, and CI
  foundation for a public GPL-3.0-or-later project.
- Added typed Roku device snapshots and sanitized ECP parsers for device, power, active app,
  screensaver, media-player, audio, volume, mute, capability, standby, and degraded-state data.
- Added a shell-free `rds` 1.0.0 adapter with exact argument-list invocations, compatibility
  diagnostics, bounded process capture and shutdown, complete endpoint merging, and per-device
  polling with tokenized claims, bounded jittered backoff, and failure isolation.
- Added serial-stable device registration with mutable addresses, persistent configuration,
  completed asynchronous discovery, snapshot invalidation, and deduplicated client interests.
- Added the D-Bus-activated `rokudeckd` service with the versioned
  `io.github.josephur.RokuDeck1` schema, typed method errors, lifecycle accounting, safe command
  validation, per-device queues, and bounded command deadlines.
- Added the `rokudeckctl` device, status, discovery, and command client with machine-readable JSON,
  quiet output, completed discovery waiting, and documented exit-code mapping.
- Added hermetic parser, transport, scheduler, private-session-bus, CLI, install, public-contract,
  and privacy checks plus an explicitly opt-in read-only hardware smoke test.
