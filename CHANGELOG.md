# Changelog

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

All notable changes to RokuDeck are documented in this file.

## Unreleased

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
