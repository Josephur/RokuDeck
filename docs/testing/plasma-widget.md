<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Plasma widget validation

RokuDeck is in alpha. Automated tests cover the daemon contract, the widget's
model and bridge, and QML behaviour, but they run offscreen and cannot tell you
whether the widget *looks* right. This page records how to run it for real and
what a human still has to confirm.

## Install a development build

```bash
bash scripts/install-dev.sh
```

The script builds into `build/install-dev` and installs into `~/.local`. It has
to configure with that prefix rather than only passing `--prefix` at install
time, because the D-Bus and systemd unit files embed the absolute path of
`rokudeckd`. It also copies the user unit into `~/.config/systemd/user`, which
is where systemd actually looks, and asks the running session bus to re-read its
service files so activation works without logging out.

A home-directory prefix is not on Plasma's or Qt's default search paths, so
export what the script prints before running any Plasma tool in that shell:

```bash
export XDG_DATA_DIRS="$HOME/.local/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export QT_PLUGIN_PATH="$HOME/.local/lib/plugins:${QT_PLUGIN_PATH:-}"
```

Remove everything again with `bash scripts/uninstall-dev.sh`. It deletes exactly
the paths CMake recorded in its install manifest, so a shared prefix such as
`~/.local` keeps everything that is not RokuDeck's. Your settings in
`~/.config/rokudeckrc` are left alone.

## Check the service layer first

If the daemon is not answering, the widget has nothing to show and every visual
check below is meaningless. Confirm the service works on its own:

```bash
rokudeckctl devices     # activates the daemon on demand and lists what it knows
rokudeckctl discover    # asks rds to search the network
rokudeckctl status <serial>
```

The daemon exits after about thirty seconds with no clients. That is expected:
`rokudeckctl` and the widget both start it again when they need it.

To add a device by hand, test the address first — the serial that identifies a
Roku only comes back from the device:

```bash
busctl --user call io.github.josephur.RokuDeck1 /io/github/josephur/RokuDeck1 \
    io.github.josephur.RokuDeck1 TestDevice s "<ipv4-address>"
busctl --user call io.github.josephur.RokuDeck1 /io/github/josephur/RokuDeck1 \
    io.github.josephur.RokuDeck1 AddDevice s \
    '{"serial":"<serial>","displayName":"<name>","address":"<ipv4-address>"}'
```

## Run the widget

```bash
plasmoidviewer -a io.github.josephur.rokudeck
```

Expected: the applet opens, the daemon is activated over D-Bus, and the selected
device renders without warnings.

Two things about `plasmoidviewer` are worth knowing before trusting what you see:

- **It only ever shows the expanded representation.** It hosts the applet in a
  desktop containment inside a window that is larger than the applet's
  `switchWidth`/`switchHeight`, so the compact representation is never created —
  passing `-f vertical` or `-l topedge` does not change that. Rows 1 to 3 below
  therefore cannot be checked here; add the widget to a real panel instead.
- **Its QML output goes to the journal, not the terminal.** Checking the console
  will show nothing at all. Read it with:

  ```bash
  journalctl --user --since "5 minutes ago" | grep plasmoidviewer
  ```

For the panel states, add RokuDeck to a real panel or the desktop: right-click
the panel, *Add Widgets…*, then search for RokuDeck. Move it between a
horizontal panel, a vertical panel and the desktop to reach rows 1 to 3.

## What a human has to confirm

Process exit status says nothing about appearance, so each row below needs
eyes on it. Record the result and the date; do not infer a pass from the
widget merely starting.

| # | State | What to look for | Result |
|---|-------|------------------|--------|
| 1 | Horizontal panel | Icon, name and status fit the panel height without clipping | not yet checked |
| 2 | Vertical panel | Labels drop out, the icon stays legible and square | not yet checked |
| 3 | Desktop (planar) | Compact form is readable at the default widget size | not yet checked |
| 4 | Expanded, wide | Device list and detail pane sit side by side | not yet checked |
| 5 | Expanded, narrow | The two panes stack instead of overlapping | not yet checked |
| 6 | Device in standby | Reads as standby, not as an error or a failure | not yet checked |
| 7 | Device offline | Reads as offline and controls are visibly disabled | not yet checked |
| 8 | Screen saver active | Shown as its own state, distinct from "Inactive" | not yet checked |
| 9 | Settings → Devices | Discovery, test connection, rename, reorder, remove all behave | not yet checked |
| 10 | Settings → Display/Controls | Hiding a row or a control actually removes it from the widget | not yet checked |
| 11 | Keyboard only | Tab reaches the device list and every control; focus is visible | not yet checked |
| 12 | Screen reader | Icon-only buttons announce a name, status changes are announced | not yet checked |

## Known gaps

- Automation rules and the per-device volume ceiling are not implemented. The
  Advanced settings page says so rather than offering settings nothing enforces.
- A device's `wakeOnWlan` preference is stored and shown but nothing acts on it
  yet; it exists so the automation backend has the user's answer when it lands.
