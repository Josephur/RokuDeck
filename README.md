# RokuDeck

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Overview

RokuDeck is a Plasma 6 project for controlling Roku devices from the desktop.

## Status

The public project foundation is in place. Runtime functionality is planned and not yet
implemented.

## Screenshots

Screenshots are planned for a future release.

## Features

Planned features include device discovery, remote controls, and safe automation controls.

## Architecture

RokuDeck will use Qt 6 and KDE Frameworks 6, with CMake as its build system.

## Requirements

- CMake 3.25 or newer
- ECM 6.0 or newer
- Qt 6.7 or newer: Core, DBus, Network, and Test
- KDE Frameworks 6.0 or newer: Config and CoreAddons
- A C++20 compiler and Ninja

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

## Install

Installation targets are planned until runtime components are implemented.

## Usage

Runtime usage is planned until RokuDeck provides a user-facing target.

## Automation Safety

Mutating TV tests are disabled by default. Keep local credentials and test-device addresses in
the ignored `.env` file; enable mutating tests only when you explicitly intend to control a TV.

## Privacy

RokuDeck is designed to keep device configuration local. Do not commit local device addresses,
credentials, or diagnostic output containing them.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

RokuDeck is licensed under [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt).
