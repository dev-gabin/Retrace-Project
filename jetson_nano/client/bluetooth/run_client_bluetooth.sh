#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SCRIPT_DIR}/client_bluetooth.conf"
EXAMPLE_FILE="${SCRIPT_DIR}/client_bluetooth.conf.example"
CLIENT_BIN="${SCRIPT_DIR}/client_bluetooth"

if [[ ! -f "${CONFIG_FILE}" ]]; then
    cp "${EXAMPLE_FILE}" "${CONFIG_FILE}"
    chmod 600 "${CONFIG_FILE}"
    printf 'Created local settings: %s\n' "${CONFIG_FILE}"
fi

# This is a user-owned local config file, intentionally excluded from Git.
# shellcheck disable=SC1090
source "${CONFIG_FILE}"

: "${RETRACE_SERVER_HOST:?Missing RETRACE_SERVER_HOST in client_bluetooth.conf}"
: "${RETRACE_SERVER_PORT:?Missing RETRACE_SERVER_PORT in client_bluetooth.conf}"
: "${RETRACE_CLIENT_ID:?Missing RETRACE_CLIENT_ID in client_bluetooth.conf}"
: "${RETRACE_BLUETOOTH_MAC:?Missing RETRACE_BLUETOOTH_MAC in client_bluetooth.conf}"
: "${RETRACE_RFCOMM_CHANNEL:?Missing RETRACE_RFCOMM_CHANNEL in client_bluetooth.conf}"

if [[ -z "${RETRACE_CLIENT_PASSWORD:-}" ]]; then
    read -r -s -p "Password for ${RETRACE_CLIENT_ID}: " RETRACE_CLIENT_PASSWORD
    printf '\n'
fi
export RETRACE_CLIENT_PASSWORD

exec "${CLIENT_BIN}" \
    "${RETRACE_SERVER_HOST}" \
    "${RETRACE_SERVER_PORT}" \
    "${RETRACE_CLIENT_ID}" \
    "${RETRACE_BLUETOOTH_MAC}" \
    "${RETRACE_RFCOMM_CHANNEL}"
