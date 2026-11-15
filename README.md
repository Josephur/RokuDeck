> [!NOTE]
> **RokuDeck is early software.** The core feature set works and is validated on real TVs and
> real peer machines, but expect rough edges, and treat the D-Bus and on-disk formats as
> unstable until a 1.0 release.

# RokuDeck

[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or-later-blue.svg)](LICENSES/GPL-3.0-or-later.txt)

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Overview

RokuDeck is a local-first controller for KDE Plasma 6 that treats Roku TVs and the Linux
machines around them as one controllable, automatable device set. A shared, D-Bus-activated
daemon owns all devices, state, and automation; the desktop widget and the `rokudeckctl` CLI
are thin clients of that one service.

## Features

- **The panel widget**: a device list with live state (power, active app, volume, mute),
  a full remote for Roku devices (power, volume, navigation, playback, app launches), device
  detail pages, and per-widget customization of the visible device set, information, and
  controls.
- **Automations**: daemon-owned rules that fire on time, interval, or device state changes,
  gated by condition trees (`all`/`any`/`not`), with per-rule execution modes
  (`single`/`restart`/`queued`), optional time-of-day windows, and honest run outcomes for
  everything (including suppressed firings). Actions cover keypresses, app launches, absolute
  volume and mute, nested conditionals, macros, and local scripts approved by name.
- **This PC**: the machine the daemon runs on is a first-class device — PipeWire volume and
  mute, MPRIS playing state, exact absolute volume control, and a per-device volume ceiling
  that every automation path honors.
- **Network peers** (experimental): other Linux machines running RokuDeck appear as devices —
  their audio state streams into your registry and your rules, and you can drive their volume
  and mute explicitly. Rules stay strictly per-machine local: a rule is stored, evaluated, and
  executed only by the daemon that owns it, and peers can revoke access at any time.
- **A script-friendly CLI**: `rokudeckctl` speaks the same D-Bus contract with typed exit
  codes and `--json` output.

## Network peers

Two Linux machines, each running the daemon, can appear in each other's device lists. Consent
is explicit and symmetric: the listening machine mints a one-time pairing code, the
controlling machine submits it along with the peer's address, and both sides store the
relationship (a rotating connection token on the controller, a paired-controller record on the
peer). Either side can revoke at any time; a revoked peer goes honestly offline.

On the machine you want to control **from elsewhere** (the peer):

```sh
rokudeckctl peers listener on     # explicit enable; binds port 49160
rokudeckctl peers code            # mint a one-time pairing code (10 minutes, single use)
```

On the machine that wants to **observe and act** (the controller):

```sh
rokudeckctl peers pair PEER_ADDRESS 49160 THE-CODE
rokudeckctl peers list            # see paired peers and paired controllers
rokudeckctl peers remove MACHINE_ID
```

The same surface lives in the widget's config dialog under **Peers**.

- **Firewall**: the peer must accept inbound TCP on the listener port. A default-deny host
  firewall (e.g. `ufw`) silently *drops* the connection — a pairing that times out rather than
  being refused almost always means this. `sudo ufw allow 49160/tcp` on the peer fixes it.
- **State and actions**: peers report volume, mute, and playing state; controllers may set
  volume (absolute, clamped by the peer's own volume ceiling) and set mute explicitly. There
  are no remote-control keypresses against a PC.
- **Rules never leave the machine that owns them** — a peer is observed data and an action
  target, never a rule host. See
  [`docs/superpowers/plans/2026-11-14-network-peers-design.md`](docs/superpowers/plans/2026-11-14-network-peers-design.md)
  for the full design, the threat model (LAN-local plaintext, ECP-equivalent), and the
  deliberately cut v2 candidates (mDNS discovery, peer-push, TLS).

## Architecture

`rokudeckd` is the only component that polls devices, executes commands, evaluates rules, or
stores configuration. `rokudeckctl` and the Plasma widget are thin clients of the generated
D-Bus proxy for `io.github.josephur.RokuDeck1`; JSON payloads use `schemaVersion: 1`.

## Requirements

- CMake 3.25 or newer
- ECM 6.0 or newer
- Qt 6.7 or newer: Core, DBus, Network, and Test
- KDE Frameworks 6.0 or newer: Config and CoreAddons
- A C++20 compiler and Ninja
- Plasma 6 for the widget (daemon-only builds use `-DROKUDECK_BUILD_PLASMOID=OFF`)
- `rds` for live Roku discovery, status, and commands; `wpctl` (PipeWire) for This-PC audio

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

The install includes `rokudeckctl`, the D-Bus-activated daemon, the Plasma widget, and the
versioned D-Bus interface and service files. Packagers may set `CMAKE_INSTALL_PREFIX` when
configuring; changing only the install-time `--prefix` does not rewrite the generated
activation paths.

For a user-local install (including the widget), or for daemon-only machines without Plasma:

```sh
bash scripts/install-dev.sh                                # full install into ~/.local
cmake -B build/vm -DROKUDECK_BUILD_PLASMOID=OFF \
      -DROKUDECK_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=~/.local  # daemon-only
```

`bash scripts/uninstall-dev.sh` removes exactly the paths CMake recorded and leaves your
settings in `~/.config/rokudeckrc` alone.

## Usage

The CLI talks only to `io.github.josephur.RokuDeck1`:

```sh
rokudeckctl devices
rokudeckctl --json devices
rokudeckctl status DEVICE
rokudeckctl discover
rokudeckctl command DEVICE Home
rokudeckctl command DEVICE '{"operation":"launch","appId":"APP_ID"}'
rokudeckctl peers list
rokudeckctl peers pair ADDRESS PORT CODE
```

Use `--json` for machine-readable output and `--quiet` to suppress successful output. A plain
`ACTION` is sent as a keypress; a JSON object is passed to the daemon's supported-command schema.
The CLI returns `2` for usage errors, `3` when the service is unavailable, `4` for an unknown
device, `5` for an unsupported command, `6` for a safety rejection, `7` for transport or
service-response failure, `8` for an invalid rule, `9` for an unknown rule, and `10` for a
rejected pairing. The daemon accepts at most four outstanding commands per device and gives
every accepted request a bounded completion deadline; the CLI waits beyond that deadline for
the matching completion signal. A saturated queue is rejected before another action is added.

Automations are managed in the widget's **Automations** page (a structured editor for every
trigger, condition, and action type) or over D-Bus with `AddRule`/`UpdateRule` JSON documents;
see the design documents in [`docs/superpowers`](docs/superpowers) for the rule schema.

## Automation Safety

Automations are safety-gated end to end: every dispatch flows through the same validated,
queued command pipeline as remote controls; volume actions clamp into a per-device ceiling
that is always on (0–100, daemon-owned, echoed to every client); audio keypresses against a
Roku are only accepted once a fresh volume reading was observed; matching mute state is a
successful no-op rather than a redundant write; and unknown or stale state never satisfies a
positive rule condition. Rules can opt into **only run while TV is on** (`PowerOn`, which
includes screensaver mode); screensaver active/inactive is a separate condition.

The rule option **Only run while TV is on** means `PowerOn`, which includes screensaver mode;
screensaver active/inactive is a separate condition. Unknown or stale state will not satisfy a
positive rule condition.

## Privacy

RokuDeck is designed to keep device configuration local. Do not commit local device addresses,
serial numbers, network names, credentials, or diagnostic output containing them. The local
`.env` file is ignored and should remain untracked. Peer pairing stores connection tokens only
in the local config file; they never appear on D-Bus or over the wire after pairing.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Security

Report vulnerabilities through the private channel in [SECURITY.md](SECURITY.md).

## License

RokuDeck is licensed under [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt).
