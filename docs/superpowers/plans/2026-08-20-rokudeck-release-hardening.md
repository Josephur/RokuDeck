# RokuDeck Public Release Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the completed version 1 implementation into a trustworthy, documented, reproducible public release candidate.

**Architecture:** Diagnostics and notifications remain daemon-owned, while CI and packaging reproduce local builds. Sanitized fixtures cover automation behavior; explicit hardware and human checklists cover physical and visual outcomes.

**Tech Stack:** CMake/CTest, Qt Test/QML Test, Clang tooling, sanitizers, REUSE/SPDX, GitHub Actions, `plasmoidviewer`, `gh`

**Spec:** `docs/superpowers/specs/2026-08-20-rokudeck-v1-design.md`

## Global Constraints

- Complete the foundation, Plasma widget, and automation plans first.
- Do not publish secrets, local device identifiers, raw diagnostics, or `.env`.
- Do not claim a visual or physical behavior passed without direct evidence; request user validation when needed.
- Keep public status and screenshots honest; label unreleased work clearly.
- Prefix local shell commands with `rtk`.

---

### Task 1: Typed diagnostics, redaction, and restrained notifications

**Files:**
- Create: `src/core/diagnostic_event.h`
- Create: `src/core/diagnostic_redactor.h`
- Create: `src/core/diagnostic_redactor.cpp`
- Create: `src/daemon/activity_store.h`
- Create: `src/daemon/activity_store.cpp`
- Create: `src/daemon/notification_sink.h`
- Create: `src/daemon/notification_sink.cpp`
- Create: `tests/unit/diagnostic_redactor_test.cpp`
- Create: `tests/unit/notification_policy_test.cpp`
- Modify: `src/plasmoid/qml/config/ConfigAdvanced.qml`

**Interfaces:**
- Consumes: device, transport, rule, and safety decisions.
- Produces: bounded activity history, redacted export, and configurable user-affecting notifications.

- [ ] **Step 1: Write failing redaction and notification tests**

```cpp
void DiagnosticRedactorTest::removesPrivateDeviceValues()
{
    const QString fixtureMac = QStringList({"aa", "bb", "cc", "dd", "ee", "ff"}).join(':');
    const QString input = "serial=SERIAL-A ip=192.0.2.8 mac=" + fixtureMac;
    const QString output = redactor.redact(input, privateValues());
    QVERIFY(!output.contains("SERIAL-A"));
    QVERIFY(!output.contains("192.0.2.8"));
    QVERIFY(!output.contains(fixtureMac));
}

void NotificationPolicyTest::standbyDoesNotNotify()
{
    QVERIFY(!policy.shouldNotify(event(DeviceHealth::Standby)));
    QVERIFY(policy.shouldNotify(event(SafetyRejection::UnmuteWithUnknownVolume)));
}
```

- [ ] **Step 2: Confirm failure, then implement bounded storage and redaction**

Keep at most 1,000 events or seven days, whichever is smaller. Redact configured serials, addresses, MAC formats, device IDs, network names, and environment values before export. Preserve timestamps in America/Indiana/Indianapolis for user-facing display while retaining raw UTC internally.

```cpp
QString DiagnosticRedactor::redact(QString text, const QStringList &privateValues) const
{
    for (const auto &value : privateValues) {
        if (!value.isEmpty()) text.replace(value, QStringLiteral("[redacted]"));
    }
    text.replace(m_macPattern, QStringLiteral("[redacted-mac]"));
    return text;
}
```

- [ ] **Step 3: Implement notification policy and UI**

Notify only for user-affecting automation failures selected in preferences. Do not notify for standby, normal idle exit, successful polling, or ordinary retry. Advanced settings provide activity filtering, copy-redacted diagnostics, clear history, and notification controls.

```cpp
bool NotificationPolicy::shouldNotify(const DiagnosticEvent &event) const
{
    return m_enabled && event.userActionRequired
        && event.category != DiagnosticCategory::Standby
        && event.category != DiagnosticCategory::RoutineRetry;
}
```

- [ ] **Step 4: Run tests and commit**

```bash
rtk ctest --preset dev -R 'diagnostic|notification' --output-on-failure
rtk git add src/core src/daemon src/plasmoid tests/unit
rtk git commit -m "feat: add privacy-safe diagnostics"
```

### Task 2: CI, static analysis, and installation verification

**Files:**
- Modify: `.github/workflows/ci.yml`
- Create: `.github/dependabot.yml`
- Create: `cmake/Sanitizers.cmake`
- Create: `tests/install/install_layout_test.cmake`
- Create: `REUSE.toml`
- Modify: `CMakePresets.json`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: all build and test targets.
- Produces: reproducible developer, CI, ASan/UBSan, and release presets.

- [ ] **Step 1: Add an install-layout test that initially fails**

The test must configure a temporary prefix and assert the daemon, CLI, applet metadata, QML files, D-Bus service, systemd user service, and license files are installed under the corresponding KDE install directories.

```cmake
execute_process(COMMAND ${CMAKE_COMMAND} --install ${build_dir} --prefix ${install_dir}
                RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "RokuDeck installation failed")
endif()
foreach(path IN ITEMS bin/rokudeckctl libexec/rokudeckd share/licenses/RokuDeck/GPL-3.0-or-later.txt)
  if(NOT EXISTS "${install_dir}/${path}")
    message(FATAL_ERROR "Missing installed file: ${path}")
  endif()
endforeach()
```

- [ ] **Step 2: Add CI jobs with exact gates**

GitHub Actions must use `container: archlinux:latest`, install `base-devel cmake ninja extra-cmake-modules clang qt6-base qt6-declarative plasma-framework kconfig kcoreaddons ki18n kirigami libpulse dbus reuse`, and run:

```yaml
- name: Configure
  run: cmake --preset ci
- name: Build
  run: cmake --build --preset ci --parallel 2
- name: Test
  run: ctest --preset ci --output-on-failure
- name: Install layout
  run: cmake --install build/ci --prefix "$RUNNER_TEMP/rokudeck"
- name: SPDX compliance
  run: reuse lint
```

Use a KDE/Qt environment that actually provides Plasma 6 development packages. Add a separate core-only sanitizer job when full Plasma packages are unavailable under sanitizers.

- [ ] **Step 3: Run the same gates locally**

```bash
rtk cmake --preset ci
rtk cmake --build --preset ci
rtk ctest --preset ci --output-on-failure
rtk cmake --preset asan
rtk cmake --build --preset asan
rtk ctest --preset asan --output-on-failure
rtk reuse lint
```

Expected: all tests and SPDX checks pass with no sanitizer findings.

- [ ] **Step 4: Commit and verify GitHub checks**

```bash
rtk git add .github cmake tests/install REUSE.toml CMakeLists.txt CMakePresets.json
rtk git commit -m "ci: add reproducible Plasma build gates"
rtk git push origin main
rtk gh run list --repo Josephur/RokuDeck --limit 5
```

Expected: the workflow for the pushed commit completes successfully.

### Task 3: Public documentation, examples, and sanitized screenshots

**Files:**
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Modify: `CONTRIBUTING.md`
- Modify: `SECURITY.md`
- Modify: `SUPPORT.md`
- Create: `docs/architecture.md`
- Create: `docs/dbus-api.md`
- Create: `docs/automation.md`
- Create: `docs/troubleshooting.md`
- Create: `examples/automations/firefox-duck.json`
- Create: `examples/automations/headphones-mute.json`
- Create: `assets/screenshots/compact.png`
- Create: `assets/screenshots/expanded.png`
- Create: `assets/screenshots/automation-editor.png`

**Interfaces:**
- Consumes: final behavior and public D-Bus/rule schemas.
- Produces: accurate contributor and user documentation.

- [ ] **Step 1: Validate every documented command against the built project**

Run every README build, install, CLI, dry-run, and uninstall command in a clean temporary build directory. Remove or correct commands that do not produce the stated outcome.

- [ ] **Step 2: Add architecture, API, automation, and troubleshooting documents**

Document component ownership, daemon activation, D-Bus methods/signals/errors, rule JSON schema, power/screensaver semantics, manual override, volume maximum, missing `rds`, standby versus offline, and redacted diagnostics. Examples must use `SERIAL-EXAMPLE` and documentation IP ranges only.

- [ ] **Step 3: Capture screenshots with human approval**

Install the verified build and ask the user to select representative devices/states that contain no private network identifiers. Capture compact, expanded, and automation-editor views. Show the exact images to the user and obtain approval before adding them to the repository.

- [ ] **Step 4: Run leak and link checks, then commit**

```bash
rtk rg -n -F -f .private-patterns --glob '!.git/**' --glob '!.env' --glob '!.private-patterns' --glob '!.superpowers/**' .
rtk git diff --check
rtk git add README.md CHANGELOG.md CONTRIBUTING.md SECURITY.md SUPPORT.md docs examples assets
rtk git commit -m "docs: prepare RokuDeck public release guide"
```

Expected: the private-value scan returns no matches and all approved assets are staged.

### Task 4: End-to-end hardware matrix and release-candidate handoff

**Files:**
- Create: `tests/hardware/README.md`
- Create: `tests/hardware/run-read-only.sh`
- Create: `tests/hardware/run-mutating.sh`
- Create: `docs/testing/release-candidate.md`
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: complete installed RokuDeck stack and local `.env`.
- Produces: reproducible machine evidence plus explicitly recorded human observations.

- [ ] **Step 1: Run the complete automated suite**

```bash
rtk cmake --build --preset dev
rtk ctest --preset dev --output-on-failure
rtk bash tests/hardware/run-read-only.sh
```

Expected: unit, integration, QML, install, and read-only device tests pass.

- [ ] **Step 2: Obtain explicit permission at the mutating-test checkpoint**

Show the exact planned actions and target alias. After the user confirms, set `ROKUDECK_ALLOW_MUTATING_TV_TESTS=1` locally and test power, screensaver detection, mute, volume reduction/restoration, headset trigger, manual override, overlapping rules, and maximum-volume clamping. Never test a loud target; use values at or below the user's configured ceiling.

- [ ] **Step 3: Ask for human physical and visual observations**

For each ambiguous result, ask one short question and wait: whether the screen woke, whether the screensaver remained active, whether audio was audible, whether manual changes were respected, and whether the widget rendered correctly in each placement. Record the local-time observation and outcome in `docs/testing/release-candidate.md` without private identifiers.

- [ ] **Step 4: Verify remote state and prepare the release candidate**

```bash
rtk git status --short
rtk git log --oneline --decorate -10
rtk git push origin main
rtk gh repo view Josephur/RokuDeck --json visibility,defaultBranchRef,url
rtk gh run list --repo Josephur/RokuDeck --limit 10
```

Expected: the worktree is clean apart from ignored local files, the repository is public, `main` matches local `HEAD`, and required checks pass.

- [ ] **Step 5: Commit the validation record**

```bash
rtk git add tests/hardware docs/testing/release-candidate.md CHANGELOG.md
rtk git commit -m "test: record RokuDeck release candidate validation"
rtk git push origin main
```
