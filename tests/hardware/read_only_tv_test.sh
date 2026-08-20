#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

if [[ -z "${ROKUDECK_TEST_TV_IP:-}" ]]; then
  echo "SKIP: set ROKUDECK_TEST_TV_IP to opt in to the read-only TV check" >&2
  exit 77
fi

if ! command -v rds >/dev/null 2>&1; then
  echo "ERROR: rds is required for the read-only TV check" >&2
  exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
  echo "ERROR: jq is required for the read-only TV check" >&2
  exit 1
fi

if ! device_info="$(rds --json --quiet --ip "$ROKUDECK_TEST_TV_IP" device info 2>/dev/null)"; then
  echo "ERROR: the read-only device-info query failed" >&2
  exit 1
fi
if ! power_state="$(jq -er \
  '.powerMode | select(type == "string" and length <= 32 and test("^[A-Za-z]+$"))' \
  <<<"$device_info")"; then
  echo "ERROR: device info did not contain a valid power state" >&2
  exit 1
fi

if ! active_app="$(rds --json --quiet --ip "$ROKUDECK_TEST_TV_IP" \
  ecp query /query/active-app 2>/dev/null)"; then
  echo "ERROR: the read-only active-app query failed" >&2
  exit 1
fi
if ! jq -e '.success == true' >/dev/null <<<"$active_app"; then
  echo "ERROR: the active-app query did not return success" >&2
  exit 1
fi

if ! audio_device="$(rds --json --quiet --ip "$ROKUDECK_TEST_TV_IP" \
  ecp query /query/audio-device 2>/dev/null)"; then
  echo "ERROR: the read-only audio-device query failed" >&2
  exit 1
fi
if ! jq -e '.success == true' >/dev/null <<<"$audio_device"; then
  echo "ERROR: the audio-device query did not return success" >&2
  exit 1
fi

unset device_info active_app audio_device
printf 'Read-only TV check passed; power state: %s\n' "$power_state"
