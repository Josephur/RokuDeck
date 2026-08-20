> [!WARNING]
> **RokuDeck is alpha software and is not considered working at this time.** Expect missing
> features, breaking changes, and defects. Do not rely on it for everyday TV control or automation.

# RokuDeck

[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](LICENSES/GPL-3.0-or-later.txt)

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Overview

RokuDeck is a local-first, multi-device Roku controller for KDE Plasma 6. Its shared daemon is
designed to serve the desktop widget, scripts, and future automation without duplicating Roku
network logic in each client.

## Status

The implemented foundation includes the Qt 6/KF6 core, an `rds` transport adapter, device-state
parsers, multi-device registration and polling, the D-Bus-activated `rokudeckd` service, and the
script-friendly `rokudeckctl` client. The version 1 D-Bus interface is
`io.github.josephur.RokuDeck1`, and its JSON payloads use `schemaVersion: 1`.

The Plasma widget and automation engine are planned work. They are not present in this foundation
and no documentation below should be read as claiming that those features work today.

## Screenshots

Screenshots are planned for a future release.

## Features

Implemented foundation capabilities:

- serial-keyed management of multiple Roku devices;
- daemon-owned discovery, snapshots, and serialized per-device commands;
- scriptable device listing, status, discovery, and supported commands through D-Bus; and
- parser, transport, scheduler, daemon-contract, and CLI integration tests.

Planned product capabilities include the Plasma widget, device controls in the panel, and safe
audio/TV automation across multiple devices. Users will choose which applications count as audio
triggers, which audio outputs and headphones are relevant, and which TVs each rule controls.

### Planned widget customization

Each Plasma widget instance will let users choose its device set, default selected device,
displayed status information, visible controls, and compact-representation behavior. Multiple
widgets may use overlapping or separate device sets while the shared daemon continues to deduplicate
backend polling. Device settings will support friendly naming and ordering alongside discovery,
manual setup, capability inspection, connection testing, polling preferences, and per-device
automation permission. This customization model is approved direction, not functionality included
in the current foundation.

## Architecture

`rokudeckd` is the only component that invokes `rds`, polls devices, or stores device
configuration. `rokudeckctl` is a thin client of the generated D-Bus proxy. The planned Plasma
widget and automation engine will use that same service boundary.

## Requirements

- CMake 3.25 or newer
- ECM 6.0 or newer
- Qt 6.7 or newer: Core, DBus, Network, and Test
- KDE Frameworks 6.0 or newer: Config and CoreAddons
- A C++20 compiler and Ninja
- `rds` for live Roku discovery, status, and commands
- `jq` only for the optional read-only hardware smoke test

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

## Install

The development preset configures activation paths for the standard `/usr` prefix. Install there
after building (normally through a package or with administrator privileges):

```sh
sudo cmake --install build/dev
```

The install includes `rokudeckctl`, the D-Bus-activated daemon, and the versioned D-Bus interface
and service files. Packagers may set `CMAKE_INSTALL_PREFIX` when configuring; changing only the
install-time `--prefix` does not rewrite the generated activation paths.

## Usage

The CLI talks only to `io.github.josephur.RokuDeck1`:

```sh
rokudeckctl devices
rokudeckctl --json devices
rokudeckctl status DEVICE
rokudeckctl discover
rokudeckctl command DEVICE Home
rokudeckctl command DEVICE '{"operation":"launch","appId":"APP_ID"}'
```

Use `--json` for machine-readable output and `--quiet` to suppress successful output. A plain
`ACTION` is sent as a keypress; a JSON object is passed to the daemon's supported-command schema.
The CLI returns `2` for usage errors, `3` when the service is unavailable, `4` for an unknown
device, `5` for an unsupported command, `6` for a safety rejection, and `7` for transport or
service-response failure. The daemon accepts at most four outstanding commands per device and
gives every accepted request a bounded completion deadline; the CLI waits beyond that deadline for
the matching completion signal. A saturated queue is rejected before another TV action is added.

## Automation Safety

The foundation includes an opt-in, read-only hardware smoke test:

```sh
ROKUDECK_TEST_TV_IP=TV_ADDRESS bash tests/hardware/read_only_tv_test.sh
```

Without `ROKUDECK_TEST_TV_IP`, it clearly skips with exit code `77`. When enabled, it runs only
device-info, active-app, and audio-device queries. It sends no POST, power, volume, or keypress
request and prints only the reported power state, never the configured address or device
identifiers.

The planned automation engine will coordinate Linux audio and Roku TV audio. A rule may react when
user-selected applications begin or stop playback, when qualifying playback starts or stops, when
the default output changes, or when a selected output or headset becomes active or inactive. Rules
will be able to mute a selected TV or lower it to a configured volume, then restore only when no
other active rule still needs that property and the TV still matches RokuDeck's last applied value.
Manual changes therefore take precedence instead of being fought by restoration.

The approved safety direction also includes a per-device maximum volume that clamps every
RokuDeck-issued path and blocks loudness-increasing commands when fresh volume state is unavailable.
The rule option **Only run while TV is on** means `PowerOn`, which includes screensaver mode;
screensaver active/inactive is a separate condition. Unknown or stale state will not satisfy a
positive rule condition. These triggers, mute/lower-volume actions, restoration rules, and guards
are design commitments for the later automation phase, not implemented features in the current
foundation.

## Privacy

RokuDeck is designed to keep device configuration local. Do not commit local device addresses,
serial numbers, network names, credentials, or diagnostic output containing them. The local
`.env` file is ignored and should remain untracked.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Security

Report vulnerabilities through the private channel in [SECURITY.md](SECURITY.md).

## License

RokuDeck is licensed under [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt).
