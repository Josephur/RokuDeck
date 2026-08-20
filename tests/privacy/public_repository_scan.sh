#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

repository_root=$(git rev-parse --show-toplevel)
cd "$repository_root"

scan_pathspec=(-- . ":(exclude)tests/privacy/public_repository_scan.sh")
forbidden_pattern='/home/[[:alnum:]_.-]+/|(^|[^0-9])(10\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}|192\.168\.[0-9]{1,3}\.[0-9]{1,3}|172\.(1[6-9]|2[0-9]|3[01])\.[0-9]{1,3}\.[0-9]{1,3})([^0-9]|$)|([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}|([[:xdigit:]]{2}-){5}[[:xdigit:]]{2}|[[:alnum:]_-]+\.local([^[:alnum:]_-]|$)|(<serial-number>|"serial(Number)?"[[:space:]]*:[[:space:]]*"|serial=)[[:alnum:]]{8,}'

if git grep -n -I -E "$forbidden_pattern" "${scan_pathspec[@]}"; then
    echo "privacy scan: tracked public files contain a home path or local-network identifier" >&2
    exit 1
fi

if [[ -s .private-patterns ]] &&
    git grep -n -I -F -f .private-patterns -- . ":(exclude)tests/privacy/public_repository_scan.sh"; then
    echo "privacy scan: tracked public files contain a private IP, serial, MAC, or network identifier" >&2
    exit 1
fi

echo "privacy scan: tracked public files are clean"
