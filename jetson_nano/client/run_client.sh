#!/usr/bin/env bash
set -euo pipefail
shopt -s extglob

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CONF="${ROOT}/clientID.conf"
EXAMPLE="${ROOT}/clientID.conf.example"
RFCOMM_BIN="${ROOT}/bluetooth/client_bluetooth"
BLE_BIN="${ROOT}/ble/client_ble"

if [[ ! -f "${CONF}" ]]; then
    cp "${EXAMPLE}" "${CONF}"
    chmod 600 "${CONF}"
fi

trim() {
    local v="$1"
    v="${v##+([[:space:]])}"
    v="${v%%+([[:space:]])}"
    printf '%s' "${v}"
}

declare -a pids=() names=()
declare -A used_ids=()
section="" type="" client_id="" password="" mac="" channel=""

cleanup() {
    trap - INT TERM EXIT
    for pid in "${pids[@]}"; do kill "${pid}" 2>/dev/null || true; done
    for pid in "${pids[@]}"; do wait "${pid}" 2>/dev/null || true; done
}
trap cleanup INT TERM EXIT

reset_values() {
    type="" client_id="" password="" mac="" channel=""
}

need() {
    [[ -n "$2" ]] || { printf '[%s] missing %s\n' "${section}" "$1" >&2; return 1; }
}

launch() {
    local secret pid
    [[ -z "${section}" ]] && return 0
    need TYPE "${type}"
    need CLIENT_ID "${client_id}"
    need CLIENT_MAC "${mac}"
    if [[ -n "${used_ids[${client_id}]:-}" ]]; then
        printf '[%s] duplicate CLIENT_ID %s; already used by [%s]\n' \
            "${section}" "${client_id}" "${used_ids[${client_id}]}" >&2
        return 1
    fi
    used_ids["${client_id}"]="${section}"
    secret="${password}"
    if [[ -z "${secret}" ]]; then
        read -r -s -p "Password for [${section}] ${client_id}: " secret
        printf '\n'
    fi

    case "${type^^}" in
        RFCOMM|BLUETOOTH)
            need CHANNEL "${channel}"
            supervise "${section}" "${client_id}" "${secret}" \
                "${RFCOMM_BIN}" "${client_id}" "${mac}" "${channel}" &
            ;;
        BLE)
            supervise "${section}" "${client_id}" "${secret}" \
                "${BLE_BIN}" "${client_id}" "${mac}" &
            ;;
        *)
            printf '[%s] unsupported TYPE=%s\n' "${section}" "${type}" >&2
            return 1
            ;;
    esac
    pid=$!
    pids+=("${pid}")
    names+=("${section}")
    printf '[%s] started %s as PID %s\n' "${section}" "${client_id}" "${pid}"
}

supervise() {
    local label="$1" id="$2" secret="$3" child status
    shift 3

    trap '[[ -n "${child:-}" ]] && kill "${child}" 2>/dev/null || true; wait "${child:-}" 2>/dev/null || true; exit 0' INT TERM

    while true; do
        RETRACE_CLIENT_PASSWORD="${secret}" "$@" &
        child=$!
        set +e
        wait "${child}"
        status=$?
        set -e
        child=""
        printf '[%s] %s disconnected (exit=%s); retrying in 3 seconds\n' \
            "${label}" "${id}" "${status}" >&2
        sleep 3
    done
}

while IFS= read -r raw || [[ -n "${raw}" ]]; do
    line="$(trim "${raw}")"
    [[ -z "${line}" || "${line}" == \#* || "${line}" == \;* ]] && continue
    if [[ "${line}" =~ ^\[([^][]+)\]$ ]]; then
        launch
        section="${BASH_REMATCH[1]}"
        reset_values
        continue
    fi
    if [[ -z "${section}" || "${line}" != *=* ]]; then
        printf 'Malformed config line: %s\n' "${raw}" >&2
        exit 1
    fi
    key="$(trim "${line%%=*}")"
    value="$(trim "${line#*=}")"
    case "${key^^}" in
        TYPE) type="${value}" ;;
        CLIENT_ID) client_id="${value}" ;;
        CLIENT_ID_PASSWD) password="${value}" ;;
        CLIENT_MAC) mac="${value}" ;;
        CHANNEL|RFCOMM_CHANNEL) channel="${value}" ;;
        *) printf '[%s] unknown key ignored: %s\n' "${section}" "${key}" >&2 ;;
    esac
done < "${CONF}"
launch

(( ${#pids[@]} > 0 )) || { printf 'No runnable clients\n' >&2; exit 1; }

wait "${pids[@]}"
