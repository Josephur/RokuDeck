# Changelog

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

All notable changes to RokuDeck are documented in this file.

## Unreleased

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
