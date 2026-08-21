#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds RokuDeck and installs it into the current user's own prefix so the
# widget can be opened in plasmoidviewer or added to a Plasma panel. Nothing
# outside the user's home directory is touched.

set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${ROKUDECK_DEV_PREFIX:-$HOME/.local}"
build_dir="${ROKUDECK_BUILD_DIR:-$project_dir/build/install-dev}"
extra_manifest="$build_dir/rokudeck_extra_installed.txt"

case "$prefix" in
    "$HOME"/*) ;;
    *)
        echo "install-dev: refusing to install outside your home directory ($prefix)" >&2
        echo "install-dev: set ROKUDECK_DEV_PREFIX to a path under $HOME" >&2
        exit 1
        ;;
esac

# The install prefix has to be known at configure time, not just at install
# time: the D-Bus and systemd unit files embed the absolute path of rokudeckd,
# so configuring for /usr and installing into $HOME would leave activation
# pointing at a binary that is not there.
cmake -S "$project_dir" -B "$build_dir" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DROKUDECK_BUILD_TESTS=OFF
cmake --build "$build_dir"

# The manifests are what uninstall-dev.sh reads back, so a stale one from an
# earlier layout must never survive an install.
rm -f "$build_dir/install_manifest.txt" "$extra_manifest"
cmake --install "$build_dir"
: >"$extra_manifest"

# ECM installs user units under <prefix>/lib/systemd/user, which systemd does
# not search for a home-directory prefix. Placing a copy where it does search
# is what makes D-Bus activation work for a development install.
systemd_unit="$prefix/lib/systemd/user/rokudeckd.service"
systemd_user_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
if [[ -f $systemd_unit ]]; then
    mkdir -p "$systemd_user_dir"
    install -m 0644 "$systemd_unit" "$systemd_user_dir/rokudeckd.service"
    echo "$systemd_user_dir/rokudeckd.service" >>"$extra_manifest"
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl --user daemon-reload || true
fi
# A session bus that is already running will not notice a newly installed
# service file on its own, so the first install would otherwise leave D-Bus
# activation broken until the next login.
if command -v busctl >/dev/null 2>&1; then
    busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus ReloadConfig >/dev/null 2>&1 || true
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

# Read the plugin directory back out of the manifest rather than guessing it:
# ECM chooses it from the platform's library layout, not from a fixed name.
plugin_root="$prefix/lib/plugins"
applet_path="$(grep -m1 'plasma/applets/io.github.josephur.rokudeck' \
    "$build_dir/install_manifest.txt" || true)"
if [[ -n $applet_path ]]; then
    plugin_root="${applet_path%/plasma/applets/*}"
fi

cat <<EOF
install-dev: installed into $prefix

A home-directory prefix is not on Plasma's or Qt's default search paths, so
export these before running Plasma tools in this shell:

    export XDG_DATA_DIRS="$prefix/share:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    export QT_PLUGIN_PATH="$plugin_root:\${QT_PLUGIN_PATH:-}"

Then open the widget on its own with:

    plasmoidviewer -a io.github.josephur.rokudeck

Remove everything again with scripts/uninstall-dev.sh.
EOF
