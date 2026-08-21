#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Removes exactly the files scripts/install-dev.sh installed, and nothing else.
# It works from CMake's own install manifest rather than deleting directories,
# so a shared prefix such as ~/.local keeps everything that is not RokuDeck's.

set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${ROKUDECK_BUILD_DIR:-$project_dir/build/install-dev}"
manifest="$build_dir/install_manifest.txt"
extra_manifest="$build_dir/rokudeck_extra_installed.txt"

if [[ ! -f $manifest ]]; then
    echo "uninstall-dev: no install manifest at $manifest" >&2
    echo "uninstall-dev: nothing recorded as installed, so nothing was removed" >&2
    exit 1
fi

if pgrep -x rokudeckd >/dev/null 2>&1; then
    if command -v systemctl >/dev/null 2>&1; then
        systemctl --user stop rokudeckd.service >/dev/null 2>&1 || true
    fi
fi

removed=0
skipped=0
while IFS= read -r installed_file; do
    [[ -n $installed_file ]] || continue
    case "$installed_file" in
        "$HOME"/*) ;;
        *)
            # Refuse to touch anything the manifest claims is outside the home
            # directory: this script only ever undoes a user-prefix install.
            echo "uninstall-dev: skipping $installed_file (outside $HOME)" >&2
            skipped=$((skipped + 1))
            continue
            ;;
    esac
    if [[ -e $installed_file || -L $installed_file ]]; then
        rm -f "$installed_file"
        removed=$((removed + 1))
    fi
done < <(cat "$manifest"; [[ -f $extra_manifest ]] && cat "$extra_manifest")

rm -f "$manifest" "$extra_manifest"

if command -v systemctl >/dev/null 2>&1; then
    systemctl --user daemon-reload || true
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

echo "uninstall-dev: removed $removed file(s), skipped $skipped"
echo "uninstall-dev: your settings in ~/.config/rokudeckrc were left alone"
