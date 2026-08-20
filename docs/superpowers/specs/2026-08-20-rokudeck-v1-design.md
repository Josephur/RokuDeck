# RokuDeck Version 1 Design

**Status:** Approved design
**Date:** August 20, 2026
**Repository:** `Josephur/RokuDeck`
**License:** GPL-3.0-or-later

## Summary

RokuDeck is a local-first Roku TV controller for Wayland and KDE Plasma with first-class automation support. Version 1 consists of a shared backend, a script-friendly command-line client, and a Plasma widget. It supports multiple Roku devices from the beginning and delegates Roku discovery, queries, and commands to the installed `rds` CLI from `paramount-engineering/roku-dev-studio`.

The initial automation focus is coordinating Linux audio with Roku TV audio. Rules can mute or reduce a TV's volume when selected Linux applications play audio or when the user switches to a headset. Every behavior is configurable, observable, and constrained by device-wide safety limits.

## Goals

- Support multiple Roku TVs as independent, first-class devices.
- Provide a polished Plasma 6 widget with compact and expanded layouts.
- Centralize device polling and commands outside `plasmashell`.
- Expose a stable D-Bus API and a scripting-oriented CLI.
- Automate TV audio from Linux playback and output-device state.
- Make power, screensaver, app, input, media, mute, and volume state usable in rule conditions.
- Prevent automations from fighting manual user changes.
- Enforce an optional device-wide maximum on every RokuDeck-issued volume command.
- Remain local-only, transparent, testable, and appropriate for a public GPL project.

## Version 1 Non-goals

- A cloud service, account system, or telemetry.
- Mobile, web, GNOME, Windows, or macOS clients.
- Reimplementing Roku ECP or device discovery independently of `rds`.
- Arbitrarily nested rule-expression trees. Version 1 supports a clear trigger plus a list of conditions that must all match.
- Guaranteeing a volume ceiling against physical remotes or third-party applications. The ceiling governs commands issued through RokuDeck.

## Platform and Dependencies

- C++20
- CMake and Ninja
- Qt 6
- KDE Frameworks 6 and Plasma 6
- QML and Kirigami for the widget interface
- D-Bus for local IPC and activation
- PulseAudio-compatible APIs for audio observation; this works with PipeWire's PulseAudio compatibility layer
- The external `rds` executable for Roku operations

The local `/home/josephur/dev/kde-reference` tree is read-only reference material. RokuDeck follows current Plasma 6 patterns from that tree and the installed Plasma widgets, including `PlasmoidItem`, C++ QML models, KConfig settings pages, and `plasmoidviewer` testing.

## Architecture

RokuDeck is one CMake project with four primary deliverables.

### `rokudeck-core`

A reusable C++ library containing:

- Device and capability models
- Immutable, timestamped device snapshots
- `rds` invocation and response parsing interfaces
- Polling and backoff policies
- Audio-observer interfaces
- Automation rule models and the rule state machine
- Command planning, conflict reconciliation, and safety guards
- Configuration and persisted action-ledger models

The core does not depend on Plasma UI types. External effects are hidden behind interfaces so tests can supply fake clocks, process runners, audio sources, and device transports.

### `rokudeckd`

A D-Bus-activated user service that owns all device and automation behavior:

- Roku discovery, registration, and stable identity
- Deduplicated device polling
- Per-device serialized command queues
- Linux audio observation
- Rule evaluation and restoration
- Global configuration and activity history
- D-Bus objects, methods, properties, and change signals

The daemon remains alive while at least one widget client is registered or at least one automation is enabled. Otherwise, it may idle-exit and be restarted through D-Bus or the user service manager. D-Bus name-owner loss removes abandoned widget registrations.

### `rokudeckctl`

A script-friendly client of the same D-Bus API. It contains no duplicate Roku logic. Human-readable output is the default, with structured JSON and quiet modes for automation. It can list devices and state, issue supported commands, inspect and test rules, pause automations, and display diagnostics.

### Plasma plugin

The plugin ID is `io.github.josephur.rokudeck`. A small C++ bridge exposes typed `QAbstractListModel` objects and commands to QML. QML and Kirigami provide the compact representation, expanded widget, configuration pages, and capability-dependent controls.

The widget never performs network or subprocess work inside `plasmashell`.

## D-Bus Contract

The versioned service API is `io.github.josephur.RokuDeck1`. Versioning is part of the public contract so future incompatible changes can coexist with version 1 clients.

The API exposes:

- Device discovery, registration, removal, ordering, and snapshots
- Capability-gated device commands
- Widget-client registration and polling-interest declarations
- Automation rule CRUD, enablement, pause, dry-run, and test operations
- Activity and diagnostics retrieval
- Configuration-change and state-change signals

Methods return typed errors for invalid devices, unsupported capabilities, stale safety data, dependency failures, timeouts, and rejected safety constraints.

## Device Identity and Configuration

The Roku serial number is the stable internal identity. IP addresses are mutable connection attributes and may be updated through rediscovery without creating duplicate devices.

Global configuration is stored in `rokudeckrc` and includes:

- Device identities, names, addresses, order, and capabilities
- Polling policy
- Device-wide safety settings
- Automation rules
- Notification and diagnostic preferences

Per-widget Plasma configuration includes presentation choices such as selected devices, displayed information, control visibility, compact-state behavior, and default selection. Multiple widgets may show overlapping or disjoint device sets without duplicating backend polling.

## `rds` Integration

`rokudeckd` invokes `rds` through `QProcess` using an explicit executable and argument list. It never constructs shell commands. Machine-facing calls use `--json`, `--quiet`, a target IP, and bounded timeouts.

The adapter is responsible for:

- Executable discovery and compatible-version diagnostics
- Process timeouts, cancellation, and exit handling
- JSON envelope parsing
- XML parsing for ECP query payloads
- Redacting sensitive values from logs
- Converting failures into typed core errors

Relevant queries include device information, active app, media player, audio device, installed apps, active input/channel, and other capability-dependent ECP endpoints. Commands include supported remote keys, power, mute, volume, app launch, and input selection.

If `rds` is missing or incompatible, RokuDeck reports the dependency problem and disables affected operations instead of repeatedly spawning failing processes.

## Verified Roku State Semantics

Live read-only testing against the local development TV established these semantics:

- A visible Home screen reports `powerMode: PowerOn`, an active app of type `home`, and no screensaver element.
- A screensaver reports `powerMode: PowerOn` and a distinct `<screensaver>` element in `/query/active-app`.
- A powered-off TV in warm standby remains reachable and reports `powerMode: Ready`.
- While powered off, `/query/active-app` may retain the previously visible app and is therefore stale for display-state decisions.
- Device, app, media, and audio queries did not wake the powered-off TV.

RokuDeck therefore obtains a fresh device-info result first. `PowerOn` means on, including screensaver mode. `Ready` means off or standby. When the TV is not confirmed on, app, input, media, and screensaver values are marked unknown for rule evaluation rather than trusting stale payloads.

No real development-device IP, serial number, MAC address, device ID, or network name belongs in the public repository.

## Device Snapshot and Health

Each snapshot includes a capture time, freshness, reachability, power state, screensaver state, active app/input, media state, audio state, capabilities, and per-field errors.

Device health is classified as:

- **Online:** Fresh data with confirmed `PowerOn`.
- **Standby:** Reachable with `powerMode: Ready`; this is expected, not an error.
- **Degraded:** Reachable, but one or more relevant queries failed.
- **Offline:** The device cannot be reached.
- **Unknown:** There is not enough fresh evidence to classify it safely.

Last-known values may remain visible with an explicit age, but automations do not consume stale values as current facts.

## Polling and Command Flow

Widget instances register the devices and information they currently display. The daemon merges those interests into one polling schedule per device. Visible expanded widgets receive the freshest useful state; compact or background consumers can use slower intervals. Enabled automations maintain only the state required by their triggers and conditions.

Unavailable devices use exponential backoff with jitter. One failing TV cannot delay or flood another.

Commands are serialized per device. Rapid volume changes are coalesced, safety checks run immediately before dispatch, and the UI shows pending state until a fresh query confirms the result. Failed commands are not silently retained for execution at an unrelated later time.

## Plasma User Experience

The approved expanded-widget direction is a master-detail layout:

- A device list provides fast switching and clear online, standby, degraded, and offline states.
- The selected device shows configurable information and capability-dependent controls.
- Primary controls include power, volume, mute, navigation, Home, Back, playback, app launch, and input selection where supported.
- Users choose which information and controls appear rather than receiving a fixed dashboard.

The compact representation adapts to horizontal panels, vertical panels, and desktop placement. It communicates the selected device, power/health state, mute state, and active-automation state without requiring the expanded view.

The settings application contains Devices, Display, Controls, Automations, and Advanced sections. Device setup supports network discovery and manual IP entry, device naming and ordering, capability inspection, connection tests, per-device automation permission, polling preference, and Wake-on-WLAN preference.

## Linux Audio Observation

The audio observer uses the native PulseAudio-compatible client API rather than polling command-line output. It tracks:

- Playback beginning and ending
- Playback by selected applications
- The current default output
- Selected sink or port changes
- Whether a configured headset is active

Rules can configure onset debounce and restoration delay so short system sounds and rapid output changes do not cause TV-volume thrashing.

## Automation Model

Each rule contains:

- A name, enabled state, and target Roku devices
- One audio trigger
- Zero or more TV, audio, and contextual conditions
- One or more TV actions
- Onset debounce, restoration delay, retry, notification, and dry-run settings

Version 1 conditions use **Match all** semantics. Nested arbitrary Boolean expressions are deferred.

### Triggers

Initial triggers include:

- Any selected Linux application begins or stops playback
- Any qualifying Linux playback begins or stops
- The default audio output changes
- A selected headset becomes active or inactive

### TV conditions

Power and screensaver state are independent predicates:

- Power: on, off/standby, or either
- Screensaver: active, inactive, or either
- Foreground: Home, a specific app, or a specific TV input
- Media: playing, paused, stopped, or idle
- Audio: muted or unmuted, plus numeric volume comparisons
- Connectivity: reachable or unreachable

The convenience option **Only run while TV is on** means the power condition is `PowerOn`. It includes screensaver mode. A separate screensaver condition lets a rule require either active or inactive screensaver state.

Unknown state does not satisfy a positive condition. In particular, stale active-app data from a TV in `Ready` state cannot satisfy app or screensaver conditions.

### Actions

Initial actions include:

- Mute or unmute
- Set or adjust volume
- Power commands where explicitly configured and supported
- User notification

RokuDeck never invents a wake or power-on action merely because another action targets an off TV.

### Rule lifecycle

The state machine is:

`Idle -> Trigger Pending -> Active -> Restore Pending -> Idle`

If a condition is false or unknown, the rule is suppressed without sending a command. Conditions are reevaluated while the trigger remains active. A rule may activate after its conditions become true and the onset debounce completes.

If a TV powers off during an active audio rule, RokuDeck relinquishes the affected properties without sending a restoration command that could wake the TV.

### Overlapping rules

The daemon reconciles all active intents for each device property:

- Mute wins over volume reduction.
- The lowest requested volume wins among simultaneous volume limits.
- Restoration occurs only after no active rule still needs that property.

Before the first write, RokuDeck records the original value and the value it applies. Restoration occurs only when the TV still matches RokuDeck's last applied value. If the user or another controller changes the property, RokuDeck relinquishes it instead of fighting the change.

## Device-wide Volume Maximum

Every configured Roku device has:

- **Enable volume maximum**, disabled by default
- **Maximum volume**, constrained to the device's supported numeric range

When enabled, the maximum is a hard invariant for every RokuDeck-issued path, including automation rules, widget controls, `rokudeckctl`, and restoration:

- Requested and restored targets above the maximum are clamped.
- RokuDeck does not issue `VolumeUp` when observed volume is already at the maximum.
- Before unmuting a TV whose current volume exceeds the maximum, RokuDeck lowers it to the maximum first.
- When current volume is unknown, commands that could increase loudness are blocked until a fresh reading is available. Mute and volume-down commands remain safe.
- Every clamp or blocked command is recorded with its source and reason.

Enabling the setting while the current TV volume is already higher does not silently alter the TV. The settings page shows the discrepancy and offers an explicit **Lower to maximum now** action.

The limit cannot constrain a physical remote or unrelated third-party controller, and the UI states that boundary clearly.

## Crash Recovery and Persistence

The action ledger persists original values, last RokuDeck-applied values, owning rules, and timestamps. After a daemon restart, RokuDeck first obtains fresh TV and trigger state. It restores only when no trigger still requires the property and the current TV value still equals the recorded RokuDeck value. Otherwise, it relinquishes the property and records why.

This prevents both stranded automation values and unsafe stale restoration.

## Error Handling and Diagnostics

The `rds` adapter converts timeouts, malformed responses, nonzero exits, unsupported endpoints, and unavailable devices into typed errors. Polling and command failures are isolated per device.

The widget shows concise, actionable status. A diagnostics view exposes recent queries, command decisions, rule transitions, retries, clamps, and suppression reasons. Exported diagnostics redact sensitive device and network identifiers.

Desktop notifications are configurable and reserved for user-affecting automation failures. Standby, routine polling retries, and other expected background behavior do not produce notification noise.

## Privacy and Security

- RokuDeck is local-only and contains no telemetry.
- Device commands and state do not leave the local network.
- `QProcess` argument lists avoid shell interpretation.
- Logs and exported diagnostics redact sensitive identifiers.
- The real test-device address lives only in a gitignored `.env` as `ROKUDECK_TEST_TV_IP`.
- `.env.example` contains a documentation-only placeholder.
- Destructive or state-changing hardware tests require an explicit opt-in flag.

## Testing Strategy

### Unit tests

Core tests use fake clocks, fake audio sources, and a fake `rds` process runner. Recorded, sanitized fixtures cover:

- `PowerOn`, `Ready`, Home, HDMI/input, and screensaver states
- Media playback states
- Mute, volume, and capabilities
- Malformed, partial, delayed, and unsupported responses
- Polling deduplication and backoff
- Rule debounce, suppression, overlap, restoration, and manual override
- Screensaver and power predicates
- Device-wide volume clamping and safe unmute behavior
- Crash-ledger recovery

### Integration tests

- D-Bus activation and API behavior
- Multiple widget registrations and name-owner cleanup
- Multiple simulated Roku devices with independent failures
- Configuration migration and persistence
- Plasma models, compact/full representations, accessibility, and settings persistence

### Hardware and visual testing

Opt-in hardware tests read `ROKUDECK_TEST_TV_IP` from the local environment. Read-only checks may run normally. Tests that change power, input, mute, volume, or launched apps require a separate explicit opt-in.

Final local verification uses the installed `rds`, `plasmoidviewer`, the actual Plasma environment, and the physical development TV.

When automated evidence cannot establish a visual, audible, timing, or physical-device result, development pauses at a clear checkpoint and asks the user for one concrete observation. Human validation is part of the verification strategy, not a reason to spin, guess, or remain stuck.

## Public Repository Standard

The initial public scaffold includes:

- Canonical GPL-3.0-or-later license text and SPDX headers
- A polished README with project status, screenshots, architecture, features, build/install instructions, examples, and privacy expectations
- `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `SUPPORT.md`, and `CHANGELOG.md`
- Issue and pull-request templates
- CMake presets, formatting and lint configuration, tests, and GitHub Actions
- D-Bus interface documentation and automation examples
- `.editorconfig`, `.env.example`, and a comprehensive `.gitignore`
- Gitignored `.env`, `.superpowers/`, build output, caches, and local diagnostics

The repository is created as public through `gh` only after the local scaffold, commits, and proportional verification are ready to publish.

## Version 1 Acceptance Criteria

- The project builds from documented commands on the target Plasma 6 system.
- `rokudeckd` activates on demand and exits when no widget or enabled automation requires it.
- Two or more simulated or physical devices behave independently from one daemon.
- The widget can select devices, show configurable information, and expose supported controls without performing backend work in `plasmashell`.
- `rokudeckctl` exercises the same D-Bus operations with structured output.
- The physical test TV is correctly distinguished among Home, screensaver, and standby without query-induced wakeup.
- Audio playback and headset triggers can mute or lower a selected TV and safely restore it.
- Power, screensaver, and other state predicates suppress or activate rules using fresh evidence.
- Manual TV changes prevent RokuDeck from restoring over the user.
- Every RokuDeck command path honors an enabled device-wide volume maximum.
- Missing `rds`, unreachable TVs, partial query failures, and daemon restarts produce bounded, understandable behavior.
- Automated checks pass, and any required human visual or physical validation is explicitly requested and recorded.
- The initial public repository contains no private device or network identifiers.
