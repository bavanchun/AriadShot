#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Process registry for integration test harnesses (docs/spec/12-testing-strategy.md §2).
# The record format is a superset of scripts/worktree.sh (adds pgid= for process-group cleanup).

process_cmdline() { [ -r "/proc/$1/cmdline" ] && tr '\0' ' ' <"/proc/$1/cmdline" 2>/dev/null | sed 's/ $//'; }
process_start() { [ -r "/proc/$1/stat" ] && sed -E 's/^.*\) //' "/proc/$1/stat" 2>/dev/null | cut -d ' ' -f 20; }
process_pgid() { [ -r "/proc/$1/stat" ] && sed -E 's/^.*\) //' "/proc/$1/stat" 2>/dev/null | cut -d ' ' -f 3; }

record_matches() {
    local pid=$1 start=$2 cmdline=$3
    [ -d "/proc/$pid" ] && [ "$(process_start "$pid")" = "$start" ] && [ "$(process_cmdline "$pid")" = "$cmdline" ]
}

default_processes_dir() {
    local root
    root=$(git rev-parse --show-toplevel 2>/dev/null || pwd -P)
    printf '%s/.agent/processes' "$root"
}

register_process() {
    local pid=${1:-} dir=${2:-}
    [ -n "$pid" ] && [ -d "/proc/$pid" ] || return 1
    [ -n "$dir" ] || dir=$(default_processes_dir)
    mkdir -p "$dir"
    printf 'start=%s\ncmdline=%s\npgid=%s\n' \
        "$(process_start "$pid")" "$(process_cmdline "$pid")" "$(process_pgid "$pid")" > "$dir/$pid"
}

stop_record() {
    local record=${1:-}
    [ -f "$record" ] || return 0
    local pid start cmdline pgid
    pid=${record##*/}
    start=$(sed -n 's/^start=//p' "$record")
    cmdline=$(sed -n 's/^cmdline=//p' "$record")
    pgid=$(sed -n 's/^pgid=//p' "$record")

    if [ -n "$pid" ] && record_matches "$pid" "$start" "$cmdline"; then
        local target="$pid"
        [ -n "$pgid" ] && [ "$pgid" -gt 1 ] && target="-$pgid"
        kill -TERM "$target" 2>/dev/null || true
        local waited=0
        while record_matches "$pid" "$start" "$cmdline" && [ "$waited" -lt 50 ]; do
            sleep 0.1
            waited=$((waited + 1))
        done
        record_matches "$pid" "$start" "$cmdline" && kill -KILL "$target" 2>/dev/null || true
    elif [ -n "$pid" ] && [ -d "/proc/$pid" ]; then
        echo "process-registry: warning: record $record does not match running process $pid (start: $start vs $(process_start "$pid"), cmdline: $cmdline vs $(process_cmdline "$pid"))" >&2
    fi
    rm -f "$record"
}

stop_process() {
    local pid=${1:-} dir=${2:-}
    [ -n "$pid" ] || return 1
    [ -n "$dir" ] || dir=$(default_processes_dir)
    stop_record "$dir/$pid"
}

stop_all_processes() {
    local dir=${1:-}
    [ -n "$dir" ] || dir=$(default_processes_dir)
    [ -d "$dir" ] || return 0
    for record in "$dir"/*; do
        [ -f "$record" ] && stop_record "$record"
    done
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    set -uo pipefail
    command=${1:-}
    shift || true
    case "$command" in
        record) register_process "$@" ;;
        stop) stop_process "$@" ;;
        stop-record) stop_record "$@" ;;
        stop-all) stop_all_processes "$@" ;;
        matches) record_matches "$@" ;;
        *) echo "usage: process-registry.sh record|stop|stop-record|stop-all|matches [args...]" >&2; exit 1 ;;
    esac
fi
