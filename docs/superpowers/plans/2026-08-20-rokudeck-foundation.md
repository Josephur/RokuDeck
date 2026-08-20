# RokuDeck Foundation and Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish a polished public repository containing a tested multi-device core, `rds` adapter, D-Bus daemon, and script-friendly CLI.

**Architecture:** A C++20 core library owns domain models and policies. `rokudeckd` is the sole process that invokes `rds`, polls devices, stores configuration, and exports the versioned D-Bus API; `rokudeckctl` consumes that API.

**Tech Stack:** CMake, Ninja, C++20, Qt 6 Core/DBus/Network/Test, KF6 Config/CoreAddons, ECM, `rds`, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-08-20-rokudeck-v1-design.md`

## Global Constraints

- Target Plasma 6 and Qt 6; do not introduce Plasma 5 APIs.
- Use `io.github.josephur.RokuDeck1` for D-Bus and `io.github.josephur.rokudeck` for the future plasmoid.
- Invoke `rds` with `QProcess` argument lists; never invoke a shell.
- Stable device identity is the Roku serial number; IP addresses are mutable.
- Never commit real IP addresses, serials, MAC addresses, device IDs, or network names.
- Store the local hardware address only as `ROKUDECK_TEST_TV_IP` in gitignored `.env`.
- License project source as GPL-3.0-or-later with SPDX headers.
- Prefix local shell commands with `rtk`.

---

### Task 1: Public repository skeleton and build contract

**Files:**
- Create: `.editorconfig`
- Create: `.gitignore`
- Create: `.clang-format`
- Create: `.clang-tidy`
- Create: `.env.example`
- Create: `.env`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/ProjectOptions.cmake`
- Create: `LICENSE`
- Create: `LICENSES/GPL-3.0-or-later.txt`
- Create: `README.md`
- Create: `CONTRIBUTING.md`
- Create: `CODE_OF_CONDUCT.md`
- Create: `SECURITY.md`
- Create: `SUPPORT.md`
- Create: `CHANGELOG.md`
- Create: `src/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/smoke/project_smoke.cpp`

**Interfaces:**
- Consumes: None.
- Produces: CMake options `ROKUDECK_BUILD_TESTS` and `ROKUDECK_ENABLE_DEVELOPER_MODE`, the shared project warning policy, and the `project-smoke` test. Later tasks add the production targets when their source files exist.

- [ ] **Step 1: Record the expected pre-scaffold configuration failure**

Run:

```bash
rtk cmake --preset dev
```

Expected: FAIL because `CMakePresets.json` does not exist.

- [ ] **Step 2: Create the build skeleton and smoke test**

Use this top-level contract:

```cmake
cmake_minimum_required(VERSION 3.25)
project(RokuDeck VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core DBus Network Test)
find_package(KF6 6.0 REQUIRED COMPONENTS Config CoreAddons)

option(ROKUDECK_BUILD_TESTS "Build RokuDeck tests" ON)
option(ROKUDECK_ENABLE_DEVELOPER_MODE "Enable strict developer warnings" ON)

include(cmake/ProjectOptions.cmake)
add_subdirectory(src)
if(ROKUDECK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
```

The smoke test must compile and run:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

class ProjectSmokeTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void qtEventLoopIsAvailable() { QVERIFY(QCoreApplication::instance()); }
};

QTEST_GUILESS_MAIN(ProjectSmokeTest)
#include "project_smoke.moc"
```

The `.gitignore` must include `.env`, `.private-patterns`, `.superpowers/`, `/build*/`, `.cache/`, `CMakeUserPresets.json`, `compile_commands.json`, local diagnostics, and editor caches. `.env.example` must contain only `ROKUDECK_TEST_TV_IP=192.0.2.10` and `ROKUDECK_ALLOW_MUTATING_TV_TESTS=0`; `.env` contains the real local test address and is never staged. Create local `.private-patterns` with one literal private identifier per line for pre-push leak scans; never stage it.

- [ ] **Step 3: Add public-project documents and canonical license text**

Use `GPL-3.0-or-later` in README badges and source headers. README sections must be: Overview, Status, Screenshots, Features, Architecture, Requirements, Build, Install, Usage, Automation Safety, Privacy, Contributing, License. Mark screenshots and unreleased runtime features honestly as planned until implemented.

```markdown
# RokuDeck
## Overview
## Status
## Screenshots
## Features
## Architecture
## Requirements
## Build
## Install
## Usage
## Automation Safety
## Privacy
## Contributing
## License
```

- [ ] **Step 4: Configure, build, and run the smoke test**

Run:

```bash
rtk cmake --preset dev
rtk cmake --build --preset dev
rtk ctest --preset dev --output-on-failure
```

Expected: configuration succeeds and `project-smoke` passes.

- [ ] **Step 5: Verify secrets and formatting, then commit**

Run:

```bash
rtk git check-ignore .env .superpowers/
rtk rg -n -F -f .private-patterns --glob '!.git/**' --glob '!.env' --glob '!.private-patterns' --glob '!.superpowers/**' .
rtk git diff --check
rtk git add .
rtk git commit -m "chore: establish public project foundation"
```

Expected: `.env` and `.superpowers/` are ignored; the private-value scan returns no matches; the commit succeeds.

### Task 2: Typed device snapshots and sanitized ECP parsers

**Files:**
- Create: `src/core/CMakeLists.txt`
- Create: `src/core/device_state.h`
- Create: `src/core/device_state.cpp`
- Create: `src/core/parse_result.h`
- Create: `src/core/device_info_parser.h`
- Create: `src/core/device_info_parser.cpp`
- Create: `src/core/active_app_parser.h`
- Create: `src/core/active_app_parser.cpp`
- Create: `src/core/audio_device_parser.h`
- Create: `src/core/audio_device_parser.cpp`
- Create: `src/core/media_player_parser.h`
- Create: `src/core/media_player_parser.cpp`
- Create: `tests/unit/device_parsers_test.cpp`
- Create: `tests/fixtures/device-info-power-on.xml`
- Create: `tests/fixtures/device-info-ready.xml`
- Create: `tests/fixtures/active-app-home.xml`
- Create: `tests/fixtures/active-app-screensaver.xml`
- Create: `tests/fixtures/audio-device.xml`
- Create: `tests/fixtures/media-player-playing.xml`
- Create: `tests/fixtures/media-player-idle.xml`

**Interfaces:**
- Consumes: Qt Core and sanitized XML fixtures.
- Produces: `PowerState`, `TriState`, `MediaPlaybackState`, `DeviceHealth`, `DeviceSnapshot`, `ParseError`, `parseDeviceInfo(QByteArrayView)`, `parseActiveApp(QByteArrayView)`, `parseAudioDevice(QByteArrayView)`, and `parseMediaPlayer(QByteArrayView)` in namespace `RokuDeck`.

- [ ] **Step 1: Write parser tests for the verified power and screensaver semantics**

```cpp
void DeviceParsersTest::readyInvalidatesDisplayState()
{
    DeviceSnapshot snapshot;
    snapshot.power = parseDeviceInfo(loadFixture("device-info-ready.xml")).value().power;
    snapshot.activeApp = parseActiveApp(loadFixture("active-app-home.xml")).value();
    snapshot.normalize();

    QCOMPARE(snapshot.power, PowerState::Standby);
    QCOMPARE(snapshot.screensaver, TriState::Unknown);
    QVERIFY(!snapshot.activeApp.has_value());
}

void DeviceParsersTest::screensaverCountsAsPowerOn()
{
    const auto device = parseDeviceInfo(loadFixture("device-info-power-on.xml")).value();
    const auto app = parseActiveApp(loadFixture("active-app-screensaver.xml")).value();
    QCOMPARE(device.power, PowerState::On);
    QCOMPARE(app.screensaver, TriState::Active);
}
```

- [ ] **Step 2: Run the focused test and confirm it fails**

Run:

```bash
rtk cmake --build --preset dev
rtk ctest --preset dev -R device-parsers --output-on-failure
```

Expected: FAIL because the parser types and functions do not exist.

- [ ] **Step 3: Implement the minimal typed model and parsers**

The public model must use these exact enum names:

```cpp
namespace RokuDeck {
enum class PowerState { Unknown, On, Standby };
enum class TriState { Unknown, Inactive, Active };
enum class MediaPlaybackState { Unknown, Playing, Paused, Stopped, Idle };
enum class DeviceHealth { Unknown, Online, Standby, Degraded, Offline };

struct ActiveAppState {
    QString id;
    QString name;
    QString type;
    QString input;
    TriState screensaver = TriState::Inactive;
};

struct DeviceSnapshot {
    QString serial;
    QString displayName;
    QString address;
    PowerState power = PowerState::Unknown;
    TriState screensaver = TriState::Unknown;
    std::optional<ActiveAppState> activeApp;
    MediaPlaybackState media = MediaPlaybackState::Unknown;
    std::optional<int> volume;
    std::optional<bool> muted;
    QSet<QString> capabilities;
    QDateTime capturedAt;
    DeviceHealth health = DeviceHealth::Unknown;
    void normalize();
};
}
```

`normalize()` must clear `activeApp`, set screensaver and media to `Unknown`, and clear other display-derived state whenever power is not `On`.

- [ ] **Step 4: Run parser and sanitizer tests**

Run:

```bash
rtk ctest --preset dev -R device-parsers --output-on-failure
rtk git diff --check
```

Expected: parser tests pass for Home, screensaver, standby, volume, mute, and malformed XML.

- [ ] **Step 5: Commit the domain model**

```bash
rtk git add src/core tests/unit tests/fixtures
rtk git commit -m "feat(core): add Roku device state parsers"
```

### Task 3: Safe `rds` process adapter and multi-device registry

**Files:**
- Create: `src/core/rds_request.h`
- Create: `src/core/process_runner.h`
- Create: `src/core/qprocess_runner.h`
- Create: `src/core/qprocess_runner.cpp`
- Create: `src/core/rds_client.h`
- Create: `src/core/rds_client.cpp`
- Create: `src/core/device_registry.h`
- Create: `src/core/device_registry.cpp`
- Create: `src/core/poll_scheduler.h`
- Create: `src/core/poll_scheduler.cpp`
- Create: `tests/support/fake_process_runner.h`
- Create: `tests/unit/rds_client_test.cpp`
- Create: `tests/unit/device_registry_test.cpp`

**Interfaces:**
- Consumes: Parser functions from Task 2.
- Produces: `ProcessRunner::start(const ProcessRequest &, ProcessCallback)`, `RdsClient::queryDevice(const QString &, SnapshotCallback)`, `DeviceRegistry::upsert(const DeviceRecord &)`, and `PollScheduler::setInterest(const PollInterest &)`.

- [ ] **Step 1: Write failing argument-safety and identity tests**

```cpp
void RdsClientTest::usesArgumentsWithoutShell()
{
    FakeProcessRunner runner;
    RdsClient client(&runner);
    client.queryDevice("192.0.2.8", [](auto) {});
    QCOMPARE(runner.lastRequest.program, QStringLiteral("rds"));
    QCOMPARE(runner.lastRequest.arguments.first(4),
             QStringList({"--json", "--quiet", "--timeout", "3000"}));
    QVERIFY(runner.lastRequest.arguments.contains("192.0.2.8"));
}

void DeviceRegistryTest::rediscoveryUpdatesAddressBySerial()
{
    DeviceRegistry registry;
    registry.upsert({"SERIAL-A", "Living Room", "192.0.2.8"});
    registry.upsert({"SERIAL-A", "Living Room", "192.0.2.9"});
    QCOMPARE(registry.devices().size(), 1);
    QCOMPARE(registry.devices().front().address, QStringLiteral("192.0.2.9"));
}
```

- [ ] **Step 2: Confirm the focused tests fail**

```bash
rtk cmake --build --preset dev
rtk ctest --preset dev -R 'rds-client|device-registry' --output-on-failure
```

Expected: FAIL because the adapter and registry are absent.

- [ ] **Step 3: Implement asynchronous process execution and typed errors**

Use this request boundary:

```cpp
struct ProcessRequest {
    QString program;
    QStringList arguments;
    std::chrono::milliseconds timeout{3000};
};

struct ProcessResult {
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
    bool timedOut = false;
};

using ProcessCallback = std::function<void(ProcessResult)>;

class ProcessRunner : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start(const ProcessRequest &request, ProcessCallback callback) = 0;
};
```

`QProcessRunner` must set the program and arguments separately, kill on timeout, cap captured output, and complete each callback exactly once. `RdsClient` must parse the CLI JSON envelope before parsing embedded ECP XML.

- [ ] **Step 4: Implement deduplicated polling and bounded backoff**

`PollInterest` must contain `clientId`, `serial`, `fields`, and `interval`. Merge interests by serial and choose the shortest requested interval. Use an injectable clock and deterministic jitter source in tests. Verify one unavailable device does not defer a healthy device's due poll.

```cpp
void PollScheduler::setInterest(const PollInterest &interest)
{
    m_interests.insert({interest.clientId, interest.serial}, interest);
    const auto matching = interestsFor(interest.serial);
    const auto interval = std::ranges::min(matching, {}, &PollInterest::interval).interval;
    schedule(interest.serial, interval);
}
```

- [ ] **Step 5: Run focused and full tests, then commit**

```bash
rtk ctest --preset dev -R 'rds-client|device-registry|poll-scheduler' --output-on-failure
rtk ctest --preset dev --output-on-failure
rtk git add src/core tests
rtk git commit -m "feat(core): add safe multi-device rds transport"
```

### Task 4: Configuration, daemon lifecycle, and versioned D-Bus API

**Files:**
- Create: `src/daemon/CMakeLists.txt`
- Create: `src/daemon/main.cpp`
- Create: `src/daemon/daemon_service.h`
- Create: `src/daemon/daemon_service.cpp`
- Create: `src/daemon/config_store.h`
- Create: `src/daemon/config_store.cpp`
- Create: `src/daemon/client_registry.h`
- Create: `src/daemon/client_registry.cpp`
- Create: `data/dbus/io.github.josephur.RokuDeck1.xml`
- Create: `data/dbus/io.github.josephur.RokuDeck1.service.in`
- Create: `data/systemd/rokudeckd.service.in`
- Create: `tests/integration/daemon_dbus_test.cpp`

**Interfaces:**
- Consumes: `DeviceRegistry`, `PollScheduler`, and `RdsClient` from Task 3.
- Produces: bus name `io.github.josephur.RokuDeck1`, object `/io/github/josephur/RokuDeck1`, and JSON methods `ListDevices`, `GetSnapshot`, `RegisterClient`, `UpdateClientInterest`, `UnregisterClient`, `DiscoverDevices`, and `SendCommand`.

- [ ] **Step 1: Write a failing private-session-bus integration test**

```cpp
void DaemonDbusTest::twoClientsShareOneDeviceInterest()
{
    auto first = call("RegisterClient", {"widget-a", QStringList{"SERIAL-A"}, 1000u});
    auto second = call("RegisterClient", {"widget-b", QStringList{"SERIAL-A"}, 5000u});
    QVERIFY(!first.isError());
    QVERIFY(!second.isError());
    QCOMPARE(testDaemon()->effectiveInterval("SERIAL-A"), 1000u);
}
```

- [ ] **Step 2: Run it on an isolated D-Bus session and confirm failure**

```bash
rtk dbus-run-session -- ctest --test-dir build/dev -R daemon-dbus --output-on-failure
```

Expected: FAIL because `rokudeckd` and its D-Bus interface are absent.

- [ ] **Step 3: Define and generate the versioned interface**

The XML must declare the exact service methods and `DeviceSnapshotChanged`, `DeviceListChanged`, and `CommandFinished` signals. Snapshot and device-list payloads are compact JSON objects with a `schemaVersion` integer so additive fields remain compatible. Generate adaptor and proxy sources with `qt_add_dbus_adaptor` and `qt_add_dbus_interface`.

```xml
<interface name="io.github.josephur.RokuDeck1">
  <method name="ListDevices"><arg direction="out" type="s" name="devicesJson"/></method>
  <method name="GetSnapshot"><arg direction="in" type="s" name="serial"/><arg direction="out" type="s" name="snapshotJson"/></method>
  <method name="RegisterClient"><arg direction="in" type="s" name="clientId"/><arg direction="in" type="as" name="serials"/><arg direction="in" type="u" name="intervalMs"/></method>
  <method name="UpdateClientInterest"><arg direction="in" type="s" name="clientId"/><arg direction="in" type="as" name="serials"/><arg direction="in" type="u" name="intervalMs"/></method>
  <method name="UnregisterClient"><arg direction="in" type="s" name="clientId"/></method>
  <method name="DiscoverDevices"><arg direction="out" type="s" name="devicesJson"/></method>
  <method name="SendCommand"><arg direction="in" type="s" name="serial"/><arg direction="in" type="s" name="commandJson"/><arg direction="out" type="s" name="requestId"/></method>
  <signal name="DeviceSnapshotChanged"><arg type="s" name="serial"/><arg type="s" name="snapshotJson"/></signal>
  <signal name="DeviceListChanged"><arg type="s" name="devicesJson"/></signal>
  <signal name="CommandFinished"><arg type="s" name="requestId"/><arg type="s" name="resultJson"/></signal>
</interface>
```

- [ ] **Step 4: Implement config and lifecycle behavior**

Store global data under `KSharedConfig::openConfig("rokudeckrc")`. Register D-Bus name-owner watches for clients. The idle timer starts only when no client remains and no enabled automation exists; it quits the event loop after 30 seconds. Standby devices remain configured and are not treated as failures.

```cpp
void DaemonService::updateIdleTimer()
{
    if (m_clients.isEmpty() && !m_automationController->hasEnabledRules()) {
        m_idleTimer.start(30s);
    } else {
        m_idleTimer.stop();
    }
}
```

- [ ] **Step 5: Install activation files and verify them**

The D-Bus service file must contain:

```ini
[D-BUS Service]
Name=io.github.josephur.RokuDeck1
Exec=@KDE_INSTALL_FULL_LIBEXECDIR@/rokudeckd
SystemdService=rokudeckd.service
```

The user service must use `Type=dbus` and `BusName=io.github.josephur.RokuDeck1`. Test installation into a temporary prefix and inspect expanded paths.

- [ ] **Step 6: Run integration tests and commit**

```bash
rtk cmake --build --preset dev
rtk dbus-run-session -- ctest --test-dir build/dev -R daemon-dbus --output-on-failure
rtk cmake --install build/dev --prefix /tmp/rokudeck-install
rtk git add src/daemon data tests/integration
rtk git commit -m "feat(daemon): add activated multi-device service"
```

### Task 5: Script-friendly CLI, live read-only contract test, and first public push

**Files:**
- Create: `src/cli/CMakeLists.txt`
- Create: `src/cli/main.cpp`
- Create: `src/cli/command_line.h`
- Create: `src/cli/command_line.cpp`
- Create: `src/cli/dbus_client.h`
- Create: `src/cli/dbus_client.cpp`
- Create: `tests/integration/rokudeckctl_test.cpp`
- Create: `tests/hardware/read_only_tv_test.sh`
- Create: `.github/workflows/ci.yml`
- Modify: `README.md`

**Interfaces:**
- Consumes: `io.github.josephur.RokuDeck1` from Task 4.
- Produces: `rokudeckctl devices`, `rokudeckctl status DEVICE`, `rokudeckctl discover`, `rokudeckctl command DEVICE ACTION`, plus global `--json` and `--quiet` options.

- [ ] **Step 1: Write failing CLI process tests**

```cpp
void RokuDeckCtlTest::jsonDeviceListIsMachineReadable()
{
    QProcess cli;
    cli.start(rokudeckctlPath(), {"--json", "devices"});
    QVERIFY(cli.waitForFinished());
    QCOMPARE(cli.exitCode(), 0);
    const auto document = QJsonDocument::fromJson(cli.readAllStandardOutput());
    QVERIFY(document.isArray());
}
```

- [ ] **Step 2: Confirm the CLI test fails, then implement the parser**

Run:

```bash
rtk dbus-run-session -- ctest --test-dir build/dev -R rokudeckctl --output-on-failure
```

Expected: FAIL because `rokudeckctl` is not built.

Implement `QCommandLineParser` subcommands and map typed D-Bus errors to documented nonzero exit codes: `2` usage, `3` unavailable service, `4` unknown device, `5` unsupported command, `6` safety rejection, and `7` transport failure.

- [ ] **Step 3: Add an opt-in read-only hardware test**

The script must exit `77` when `ROKUDECK_TEST_TV_IP` is absent. When present, it runs only `rds device info`, `/query/active-app`, and `/query/audio-device`, validates JSON, and never sends POST or keypress requests.

```bash
if [[ -z "${ROKUDECK_TEST_TV_IP:-}" ]]; then
  exit 77
fi
rds --json --quiet --ip "$ROKUDECK_TEST_TV_IP" device info | jq -e '.powerMode'
rds --json --quiet --ip "$ROKUDECK_TEST_TV_IP" ecp query /query/active-app | jq -e '.success == true'
rds --json --quiet --ip "$ROKUDECK_TEST_TV_IP" ecp query /query/audio-device | jq -e '.success == true'
```

- [ ] **Step 4: Run local verification and request human evidence if needed**

```bash
rtk cmake --build --preset dev
rtk ctest --preset dev --output-on-failure
rtk dbus-run-session -- build/dev/src/cli/rokudeckctl --json devices
rtk bash tests/hardware/read_only_tv_test.sh
```

Expected: automated tests pass; the hardware test reports the local TV's power state without changing it. If the physical display result cannot be inferred, ask the user one concrete question and record the answer in the test notes.

- [ ] **Step 5: Commit and create the public GitHub repository**

```bash
rtk git add src/cli tests/hardware .github README.md
rtk git commit -m "feat(cli): add headless RokuDeck controller"
rtk git status --short
rtk gh repo create Josephur/RokuDeck --public --source=. --remote=origin --push --description "A multi-device Roku controller for KDE Plasma with first-class automation"
rtk gh repo view Josephur/RokuDeck --json nameWithOwner,visibility,url,defaultBranchRef
```

Expected: only ignored local files remain; GitHub reports `PUBLIC`, default branch `main`, and the pushed commit matches local `HEAD`.
