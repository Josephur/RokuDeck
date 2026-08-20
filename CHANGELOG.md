# Changelog

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

All notable changes to RokuDeck are documented in this file.

## Unreleased

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
