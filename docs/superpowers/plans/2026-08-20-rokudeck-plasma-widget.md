# RokuDeck Plasma Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved adaptive Plasma 6 widget and device settings UI on top of the versioned daemon API.

**Architecture:** A C++ Plasma plugin owns D-Bus proxies and typed list models. QML/Kirigami renders compact, master-detail, and configuration views without performing subprocess or network work.

**Tech Stack:** Plasma 6, Qt 6 QML/Quick/DBus/Test, KF6 I18n/Config, Kirigami, CMake `plasma_add_applet`, `plasmoidviewer`

**Spec:** `docs/superpowers/specs/2026-08-20-rokudeck-v1-design.md`

## Global Constraints

- Complete `docs/superpowers/plans/2026-08-20-rokudeck-foundation.md` first.
- Use plugin ID `io.github.josephur.rokudeck` and D-Bus API `io.github.josephur.RokuDeck1`.
- Follow Plasma 6 patterns from `/home/josephur/dev/kde-reference` and installed Plasma widgets; never modify either reference location.
- The widget must not invoke `rds`, open sockets, or poll independently.
- Every control is capability-gated and configurable per widget.
- Prefix local shell commands with `rtk`.

---

### Task 1: C++ Plasma plugin and D-Bus-backed device model

**Files:**
- Create: `src/plasmoid/CMakeLists.txt`
- Create: `src/plasmoid/rokudeckapplet.h`
- Create: `src/plasmoid/rokudeckapplet.cpp`
- Create: `src/plasmoid/devicesmodel.h`
- Create: `src/plasmoid/devicesmodel.cpp`
- Create: `src/plasmoid/daemonproxy.h`
- Create: `src/plasmoid/daemonproxy.cpp`
- Create: `src/plasmoid/metadata.json`
- Create: `tests/unit/devices_model_test.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: generated D-Bus proxy and JSON schema from the foundation plan.
- Produces: QML singleton `RokuDeckBridge`; model roles `serial`, `name`, `address`, `health`, `power`, `screensaver`, `activeLabel`, `volume`, `muted`, `capabilities`, `stale`, and `pendingCommand`.

- [ ] **Step 1: Write a failing model-role and update test**

```cpp
void DevicesModelTest::updatesOneDeviceWithoutResettingTheModel()
{
    DevicesModel model;
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.replaceDevices(deviceArray({device("SERIAL-A", "Living Room")}));
    model.applySnapshot(snapshot("SERIAL-A", 45, false));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DevicesModel::VolumeRole).toInt(), 45);
    QCOMPARE(changed.count(), 1);
}
```

- [ ] **Step 2: Confirm the model test fails**

```bash
rtk cmake --build --preset dev
rtk ctest --preset dev -R devices-model --output-on-failure
```

Expected: FAIL because `DevicesModel` is absent.

- [ ] **Step 3: Implement focused model and proxy classes**

Declare roles as a stable enum beginning at `Qt::UserRole + 1`. Parse daemon JSON once in `DaemonProxy`, pass typed values to `DevicesModel`, and update only changed roles. `RokuDeckApplet` registers and unregisters its UUID with the daemon as applet lifecycle changes.

```cpp
QHash<int, QByteArray> DevicesModel::roleNames() const
{
    return {{SerialRole, "serial"}, {NameRole, "name"}, {HealthRole, "health"},
            {PowerRole, "power"}, {ScreensaverRole, "screensaver"},
            {ActiveLabelRole, "activeLabel"}, {VolumeRole, "volume"},
            {MutedRole, "muted"}, {CapabilitiesRole, "capabilities"},
            {StaleRole, "stale"}, {PendingCommandRole, "pendingCommand"}};
}
```

- [ ] **Step 4: Build the applet with the Plasma 6 helper**

Use:

```cmake
plasma_add_applet(io.github.josephur.rokudeck
    QML_SOURCES
        qml/main.qml
        qml/CompactRepresentation.qml
        qml/FullRepresentation.qml
        qml/DeviceListItem.qml
        qml/DeviceDetail.qml
    CPP_SOURCES
        rokudeckapplet.cpp
        daemonproxy.cpp
        devicesmodel.cpp
    GENERATE_APPLET_CLASS
)
target_link_libraries(io.github.josephur.rokudeck PRIVATE
    Qt6::DBus Qt6::Qml Qt6::Quick Plasma::Plasma KF6::I18n KF6::ConfigCore)
```

- [ ] **Step 5: Run tests and commit**

```bash
rtk ctest --preset dev -R devices-model --output-on-failure
rtk git add src/plasmoid src/CMakeLists.txt tests/unit
rtk git commit -m "feat(widget): add daemon-backed Plasma model"
```

### Task 2: Adaptive compact and master-detail expanded interface

**Files:**
- Create: `src/plasmoid/qml/main.qml`
- Create: `src/plasmoid/qml/CompactRepresentation.qml`
- Create: `src/plasmoid/qml/FullRepresentation.qml`
- Create: `src/plasmoid/qml/DeviceListItem.qml`
- Create: `src/plasmoid/qml/DeviceDetail.qml`
- Create: `src/plasmoid/qml/RemoteControls.qml`
- Create: `src/plasmoid/qml/StatusBadge.qml`
- Create: `tests/qml/tst_compact.qml`
- Create: `tests/qml/tst_fullrepresentation.qml`

**Interfaces:**
- Consumes: `RokuDeckBridge.devices`, `selectedSerial`, and `sendCommand(serial, action, value)`.
- Produces: adaptive compact states and the approved master-detail expanded view.

- [ ] **Step 1: Write failing QML tests for layout and capability gating**

```qml
function test_volumeControlsFollowCapabilities() {
    bridge.setFixtureDevice({ serial: "SERIAL-A", capabilities: ["power"] })
    compare(findChild(fullView, "volumeControls").visible, false)
    bridge.setFixtureDevice({ serial: "SERIAL-A", capabilities: ["power", "volume"] })
    compare(findChild(fullView, "volumeControls").visible, true)
}
```

- [ ] **Step 2: Confirm QML tests fail**

```bash
rtk ctest --preset dev -R plasmoid-qml --output-on-failure
```

Expected: FAIL because the QML components are missing.

- [ ] **Step 3: Implement the approved representations**

`main.qml` must use `PlasmoidItem`, set `compactRepresentation` and `fullRepresentation`, and expose contextual actions for refresh and settings. Compact mode adapts to horizontal panel, vertical panel, and desktop constraints. Expanded mode uses a device list and one selected-device detail pane; narrow widths stack the panes.

```qml
PlasmoidItem {
    id: root
    compactRepresentation: CompactRepresentation { bridge: RokuDeckBridge }
    fullRepresentation: FullRepresentation {
        bridge: RokuDeckBridge
        narrow: width < Kirigami.Units.gridUnit * 32
    }
}
```

Display explicit states for online, standby, degraded, offline, stale, muted, and automation-active. Do not represent standby as an error. Buttons call the bridge and show pending state until the model confirms the command.

- [ ] **Step 4: Verify keyboard and screen-reader behavior**

Give every icon-only button an accessible name, keep native focus order, and expose status changes through accessible descriptions. Add QML tests that tab through device selection and primary controls.

```qml
PlasmaComponents.ToolButton {
    icon.name: "audio-volume-muted"
    text: i18n("Mute %1", deviceName)
    display: QQC2.AbstractButton.IconOnly
    Accessible.name: text
    onClicked: bridge.sendCommand(serial, "mute", true)
}
```

- [ ] **Step 5: Run QML tests and commit**

```bash
rtk ctest --preset dev -R plasmoid-qml --output-on-failure
rtk git add src/plasmoid/qml tests/qml
rtk git commit -m "feat(widget): add adaptive Roku control interface"
```

### Task 3: Device, display, and control configuration

**Files:**
- Create: `src/plasmoid/qml/config/config.qml`
- Create: `src/plasmoid/qml/config/ConfigDevices.qml`
- Create: `src/plasmoid/qml/config/ConfigDisplay.qml`
- Create: `src/plasmoid/qml/config/ConfigControls.qml`
- Create: `src/plasmoid/qml/config/ConfigAdvanced.qml`
- Create: `src/plasmoid/config/main.xml`
- Create: `tests/qml/tst_configuration.qml`

**Interfaces:**
- Consumes: daemon device CRUD/test/discovery methods and per-widget Plasma configuration.
- Produces: Devices, Display, Controls, and Advanced settings pages; the Automations page and device-wide volume maximum are completed by the automation plan when their enforcement backend exists.

- [ ] **Step 1: Write failing configuration persistence tests**

```qml
function test_selectedDevicesPersistPerWidget() {
    page.selectedSerials = ["SERIAL-A", "SERIAL-B"]
    page.save()
    compare(plasmoid.configuration.selectedSerials, ["SERIAL-A", "SERIAL-B"])
}
```

- [ ] **Step 2: Confirm failure, then implement KConfig metadata**

```bash
rtk ctest --preset dev -R plasmoid-config --output-on-failure
```

Define per-widget keys `selectedSerials`, `defaultSerial`, `visibleInformation`, `visibleControls`, and `compactMode`. Keep device address, safety maximum, and discovery data in daemon-owned `rokudeckrc`.

- [ ] **Step 3: Implement device management**

The Devices page must support discovery, manual IPv4 entry, rename, reorder, remove, identify, test connection, allow automations, Wake-on-WLAN preference, and refresh policy. The automation plan adds the disabled-by-default device-wide volume maximum and **Lower to maximum now** after the guard is implemented.

```qml
Kirigami.FormLayout {
    QQC2.TextField {
        Kirigami.FormData.label: i18n("Network address:")
        text: deviceAddress
        validator: RegularExpressionValidator {
            regularExpression: /^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$/
        }
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Refresh policy:")
        model: [i18n("Responsive"), i18n("Balanced"), i18n("Battery conscious")]
    }
}
```

Validate addresses and numeric limits before enabling Apply. Display detected capabilities and fresh current power/input/volume data. Never place the local test IP in QML or metadata.

- [ ] **Step 4: Run configuration tests and commit**

```bash
rtk ctest --preset dev -R plasmoid-config --output-on-failure
rtk git add src/plasmoid/qml/config src/plasmoid/config tests/qml
rtk git commit -m "feat(widget): add configurable multi-device settings"
```

### Task 4: Local install, `plasmoidviewer`, and human visual validation

**Files:**
- Create: `scripts/install-dev.sh`
- Create: `scripts/uninstall-dev.sh`
- Create: `docs/testing/plasma-widget.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: built applet and daemon.
- Produces: repeatable local install/uninstall and a recorded visual-validation checklist.

- [ ] **Step 1: Add safe development install scripts**

Scripts must use `set -euo pipefail`, resolve the repository directory, install only the project build into a documented user prefix, and remove only the exact RokuDeck-installed paths. They must never modify KDE reference sources or `/usr/share/plasma/plasmoids` directly.

```bash
#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake --install "$project_dir/build/dev" --prefix "${XDG_DATA_HOME:-$HOME/.local/share}/rokudeck-prefix"
```

- [ ] **Step 2: Build and launch in `plasmoidviewer`**

```bash
rtk cmake --build --preset dev
rtk bash scripts/install-dev.sh
rtk plasmoidviewer -a io.github.josephur.rokudeck
```

Expected: the applet opens, D-Bus activates the daemon, and the selected device renders without QML warnings.

- [ ] **Step 3: Request human visual verification**

Ask the user to validate one state at a time: horizontal panel, vertical panel, desktop compact, expanded master-detail, standby, offline, and device configuration. Record pass/fail notes in `docs/testing/plasma-widget.md`; do not infer appearance from process success.

- [ ] **Step 4: Run all tests and commit**

```bash
rtk ctest --preset dev --output-on-failure
rtk git diff --check
rtk git add scripts docs/testing README.md
rtk git commit -m "test(widget): add Plasma runtime validation workflow"
rtk git push origin main
```
