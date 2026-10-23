# RokuDeck — Agent / Contributor Notes

Project-specific gotchas that have caused shipped bugs or wasted hours. Read
before touching the areas named here. When a new lesson is learned the hard
way, add it here — that is the point of this file.

## QML dialogs (src/plasmoid/qml) — the rule-editor bug class

Three user-visible bugs shipped from one family. The mechanics, verified
against `/usr/lib/qt6/qml/org/kde/kirigami/dialogs/Dialog.qml`:

1. **`preferredWidth` is disconnected when you override `contentItem`.**
   Kirigami.Dialog's `preferredWidth` only feeds its DEFAULT contentItem
   wrapper. `ConfigRuleEditor.qml` overrides `contentItem` with a Flickable,
   so the dialog collapsed to minimum content width (~220px). Never size this
   dialog (or any dialog with a custom contentItem) via `preferredWidth` —
   set explicit `width`/`height` bindings (they are honored unconditionally),
   clamped to `parent`.
2. **`clip: true` on the editor Flickable cuts off ComboBox popups.** QQC2
   ComboBox popups render inside the control's item subtree, so ANY ancestor
   clip truncates the dropdown list. The editor's scroll area must stay
   `clip: false` (the dialog frame provides the visual boundary; scroll
   overshoot lands in invisible padding).
3. **Never re-parent combo popups to an overlay** (`popup.parent:
   applicationWindow().overlay`). It escapes the clip but breaks Qt's popup
   coordinate mapping — popups spawn at the window origin, behind the modal.

**Guard:** `tests/qml/tst_ruleeditor.qml` pins all three (explicit sizing,
`ruleEditorScroll.clip === false`, mute combo contents). It mutation-checks:
re-adding `clip: true` fails it. Extend it when adding new editor surfaces.

**qmllint is NOT a guard here** — verified by negative control, it silently
accepts unknown `PlasmaExtras.*` / `PlasmaComponents.*` types in this setup.
Two wrong-type bugs (`PlasmaExtras.PlaceHolder`, `PlasmaExtras.ToolButton`)
reached users this way. When using a plasma/kirigami type for the first time,
check it exists: `ls /usr/lib/qt6/qml/org/kde/plasma/{extras,components}/`.
Valid names seen in this repo: extras → `Heading`, `PlaceholderMessage`;
components → `Label`, `ComboBox`, `ToolButton`; extras has NO `ToolButton`,
NO `PlaceHolder`. Same rule for themed properties: `Kirigami.Theme` has
`defaultFont`/`smallFont`/`fixedWidthFont` — `smallestFont` does not exist
(it silently renders a TypeError at runtime).

## QML form state (plain-JS objects)

- **Plain JS objects are not observable.** Validation that reads
  `formState` only re-runs when a tracked property changes. Every editor
  handler that mutates the form must bump the revision
  (`ConfigRuleEditor.touchForm()`), or Save silently stays disabled.
- **Values round-tripped through `property var` / signals can arrive as
  array-likes where `Array.isArray()` is false.** Use shape checks
  (`rulepayload.isValuePair`) for `between` ranges; `Array.isArray` silently
  rejects an editor value it just wrote.
- **A `checked:` / `currentIndex:` binding dies on the first user write.**
  Controls that mirror a daemon record must report the click and re-arm the
  binding (`onClicked: { bridge.call(...); checked = Qt.binding(...) }`), or a
  rejected daemon change leaves the control showing a value the device does
  not have. `nextCheckState` exists on `CheckBox` only — not on
  `RadioButton`/`Switch` in Qt 6.12.
- **A conditional action needs a seeded node.** `ifThenElse` with `cond: null`
  cannot be edited (the kind picker dereferences it); `ActionListEditor.append`
  seeds `RulePayload.blankLeaf()`.

## Toolchain

- **GCC 16 (this machine) rejects declaration + in-class inline definition**
  ("cannot be overloaded with"). Define QtTest slot bodies OUTSIDE the class
  with `ClassName::slot()` qualification. Every existing test file does this.
- **`daemon-dbus-test` needs its private session bus and isolated XDG dirs** —
  run it via `ctest` (or duplicate the `dbus-run-session` + `XDG_DATA_DIRS`
  wrapper from `tests/CMakeLists.txt`), or `registerOnBus()` fails instantly.
- **clangd/LSP diagnostics are pre-existing include-path noise** across this
  repo ("QHash file not found" etc.). The `-Werror` ninja build is the only
  real compile gate. Never chase LSP errors.
- **`rtk` wrappers around shell tools can lie** in pipelines (a `grep -qx`
  inside `rtk | rtk` reported existing files as missing). Re-verify surprising
  results with plain shell before acting.

## Debugging user-reported Plasma issues

The fast loop that works: user reproduces →
`journalctl --user -u plasma-plasmashell.service --since "-30 min"` →
QML errors name the exact file:line and type. Both dialog bugs were
diagnosed in one pass this way. Screenshots from
`~/Pictures/Screenshots/` show rendered geometry that code review cannot.

## Deploy

`bash scripts/install-dev.sh` installs + recycles `rokudeckd` automatically,
but Plasma keeps the OLD applet plugin until
`systemctl --user restart plasma-plasmashell.service`. Daemon-only changes
need no Plasma restart. Verify deploys with
`ls -la ~/.local/lib/plugins/plasma/applets/io.github.josephur.rokudeck.so`
against the build artifact timestamp.
