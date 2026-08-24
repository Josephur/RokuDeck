# Automations M5 Implementation Plan — Volume Ceiling End-to-End

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development adapted to milestone-executor mode (one executor owns all tasks; controller reviews the branch). Steps use checkbox syntax.

**Goal:** A per-device volume ceiling becomes an always-on policy: stored on the device, honored by automation volume walks (absolute targets clamp into `[0, ceiling]`; relative up-nudges are skipped at/above the ceiling), editable from the widget's device detail page, and echoed through every device JSON surface.

**Architecture:** `DeviceRecord` gains `volumeCeiling` (plain `int`, default **100 = unrestricted**, validated 0..100). It flows through the existing device persistence/validation/merge machinery, the device JSON envelope, and the widget's devices plumbing. Enforcement happens where automation audio presses are computed (`dispatchActionList`): the setVolume target clamps per target device, and `VolumeUp` presses are skipped when the latest observed volume is already at/above the ceiling (best-effort per spec §2).

**Tech Stack:** C++20, Qt 6 Core/DBus/Test, KConfig, QtTest, Plasma QML.

**Spec:** `docs/superpowers/specs/2026-08-23-automations-design.md` §2 (ceiling semantics), §5 (storage style), §6 (`VolumeCeiling` added to `UpdateDevice` changes JSON + echoed in device JSON)

## Global Constraints

- `QStringLiteral(...)` / `_s` literals only; `-Werror`; no exceptions.
- Full suite green under `ctest` from your worktree's own build dir before every commit.
- Commit convention: `type(scope): lowercase imperative`.
- Out-of-house GCC quirk: define test slot bodies OUTSIDE the class.

## Rulings

1. **Default 100 = unrestricted.** A ceiling of 100 behaves identically to none; no separate "off" flag.
2. **M5 enforces the ceiling on automation paths only** (setVolume target clamp + automation `VolumeUp` skip). Widget-remote `SendCommand` VolumeUp enforcement is deferred — silently swallowing a remote press or erroring the remote UI both need UX design that doesn't exist yet. Documented in CHANGELOG wording ("automation volume actions honour the ceiling").
3. `mergeDevice` PreserveSettings copies `volumeCeiling` forward (discovery/polling never supply one); ApplySettings applies it; the changed-detection comparison includes it so edits announce.
4. Widget JSON: `"volumeCeiling"` appears in every device object; absent on read (old daemon/file) means default 100.

## Tasks (self-execute in order, TDD, one commit each)

### Task 1: Record + persistence + validation
- Modify: `src/core/device_registry.h` (add `int volumeCeiling = 100;` beside `refreshPolicy`), `src/daemon/config_store.cpp` (`readEntry("VolumeCeiling", 100)` / `writeEntry`), `src/daemon/daemon_service.cpp` (`validateDeviceJson`: allowedKeys += `volumeCeiling`; accept only integral doubles 0..100 — reuse the finite/trunc pattern from snapshot parsing; `mergeDevice` copy-forward + change detection; `deviceObject` echoes `"volumeCeiling"`).
- Test: extend `tests/unit/config_store_test.cpp` (round-trip preserves ceiling; absent key defaults 100) and `tests/integration/daemon_dbus_test.cpp` (`UpdateDevice {"volumeCeiling":50}` persists across recreateDaemon and appears in `ListDevices`; rejects 101, `"abc"`, non-integral 50.5 with InvalidArgs).
- RED first; commit `feat(daemon): per-device volume ceiling preference`.

### Task 2: Automation enforcement
- Modify: `src/daemon/daemon_service.cpp` `dispatchActionList` mute/setVolume expansion:
  - Per target serial compute `ceiling` from that device's record (missing device ⇒ existing rejected path).
  - `setVolume`: `target = min(action.volume, ceiling)` BEFORE delta; `delta==0` ⇒ no-op success (covers "already at/under ceiling").
  - Relative presses (`VolumeUp` from setVolume positive deltas): skip individual presses when latest `snapshot.volume >= ceiling` (re-read `m_snapshots.value(serial)` at expansion time; best-effort, spec §2 language). `VolumeDown` unaffected.
- Test: integration cases — ceiling 50, observed 10, `setVolume 90` → exactly `min(80,30)=30` VolumeUp presses (clamp target 50 ⇒ delta 40, cap 30); ceiling 50, observed 50, `setVolume 20` → 30 VolumeDown; ceiling 50, observed 50, `setVolume 60` → zero presses, outcome ok (no-op under clamp); mixed ceiling/no-ceiling devices partial path still truthful.
- Commit `feat(daemon): automation volume actions honour the ceiling`.

### Task 3: Widget plumb + settings control
- Modify: `src/plasmoid/devicesmodel.{h,cpp}` (`DeviceDescription::volumeCeiling = 100`, `VolumeCeilingRole`), `src/plasmoid/daemonproxy.cpp` (`parseDevices` reads optional integral, default 100), device detail QML (`src/plasmoid/qml/DeviceDetail.qml` — add a 0..100 spin/slider row labelled "Volume ceiling", `onValueModified` → `bridge.updateDevice(serial, {"volumeCeiling": value})`, mirroring how existing daemon-owned preferences on that page submit; show caption "Automations never set the volume above this").
- Test: `tests/unit/plasmoid_bridge_test.cpp` — FakeDaemon device fixture carries `volumeCeiling`; model role surfaces it; malformed (string) payload keeps prior behavior (parseDevices returns nullopt → stale+error, consistent with existing strictness).
- Commit `feat(plasmoid): volume ceiling control on the device page`.

### Task 4: Hygiene
- CHANGELOG bullets (ceiling storage/validation, automation enforcement best-effort wording per ruling 2, device-page control). Full suite. Final commit `docs(changelog): volume ceiling policy`.
- Do NOT run scripts/install-dev.sh — the controller deploys after review.

## Self-review checklist for the executor
Spec coverage: §2 clamp+best-effort skip (T2), §5-style storage (T1), §6 envelope echo (T1/T3), settings surface (T3). Type consistency: `volumeCeiling` spelled identically everywhere. No placeholders: every behavior above is pinned; where QML layout is unspecified, mirror the page's existing control patterns exactly.
