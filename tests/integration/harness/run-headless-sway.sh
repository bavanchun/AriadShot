#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Headless Sway test harness for tier-2 Wayland integration tests (docs/spec/12-testing-strategy.md §2).

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(git rev-parse --show-toplevel 2>/dev/null || pwd -P)
default_processes_dir="$repo_root/.agent/processes"
conf_file="$script_dir/sway-headless.conf"

# shellcheck source=/dev/null
source "$script_dir/process-registry.sh"

die() { echo "run-headless-sway: $1" >&2; exit 1; }

socket_path=""
processes_dir="$default_processes_dir"
started_pid=""
started_wayland_sock=""

parse_options() {
    remaining_cmd=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --socket) [ $# -ge 2 ] || die "--socket requires an argument"; socket_path=$2; shift 2 ;;
            --processes-dir) [ $# -ge 2 ] || die "--processes-dir requires an argument"; processes_dir=$2; shift 2 ;;
            --) shift; remaining_cmd=("$@"); break ;;
            *) remaining_cmd=("$@"); break ;;
        esac
    done
    export socket_path processes_dir
}

_stop_sway_state() {
    [ -f "$1" ] || return 0
    local pid runtime_dir sf_sock
    pid=$(sed -n 's/^pid=//p' "$1")
    runtime_dir=$(sed -n 's/^runtime_dir=//p' "$1")
    sf_sock=$(sed -n 's/^socket_path=//p' "$1")
    [ -z "$pid" ] || stop_process "$pid" "$processes_dir"
    [ -z "$sf_sock" ] || rm -f "$sf_sock"
    [ -z "$runtime_dir" ] || rm -rf "$runtime_dir"
    rm -f "$1"
}

start_sway() {
    mkdir -p "$processes_dir"
    [ -z "$socket_path" ] || stop_sway

    local runtime_dir sway_log sway_bin system_sway
    runtime_dir=$(mktemp -d -t ariad-sway-XXXXXX)
    chmod 0700 "$runtime_dir"
    sway_log="$runtime_dir/sway.log"

    system_sway=$(type -p sway || command -v sway || true)
    [ -n "$system_sway" ] && [ -x "$system_sway" ] || { rm -rf "$runtime_dir"; die "sway binary not found in PATH"; }
    sway_bin="$runtime_dir/sway"
    cp "$system_sway" "$sway_bin" || { rm -rf "$runtime_dir"; die "failed to copy sway binary to $runtime_dir"; }
    chmod 0700 "$sway_bin"

    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u DISPLAY -u SWAYSOCK -u HYPRLAND_INSTANCE_SIGNATURE \
        WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 XDG_RUNTIME_DIR="$runtime_dir" \
        setsid "$sway_bin" -c "$conf_file" > "$sway_log" 2>&1 &
    local sway_pid=$!

    local waited=0 wayland_sock="" swaysock=""
    while [ "$waited" -lt 50 ]; do
        if ! kill -0 "$sway_pid" 2>/dev/null; then
            cat "$sway_log" >&2 || true
            local exit_code=0
            wait "$sway_pid" 2>/dev/null || exit_code=$?
            kill -TERM "$sway_pid" 2>/dev/null || true
            rm -rf "$runtime_dir"
            die "Sway exited prematurely with code $exit_code (log above)"
        fi
        wayland_sock=$(find "$runtime_dir" -maxdepth 1 -name 'wayland-*' ! -name '*.lock' 2>/dev/null | head -n 1 || true)
        swaysock=$(find "$runtime_dir" -maxdepth 1 -name 'sway-ipc.*.sock' 2>/dev/null | head -n 1 || true)
        [ -n "$wayland_sock" ] && [ -n "$swaysock" ] && [ -S "$wayland_sock" ] && [ -S "$swaysock" ] && break
        sleep 0.1
        waited=$((waited + 1))
    done

    if [ -z "$wayland_sock" ] || [ -z "$swaysock" ]; then
        cat "$sway_log" >&2 || true
        kill -TERM "$sway_pid" 2>/dev/null || true
        sleep 0.2
        kill -KILL "$sway_pid" 2>/dev/null || true
        rm -rf "$runtime_dir"
        die "Timed out waiting for Sway sockets in $runtime_dir"
    fi

    register_process "$sway_pid" "$processes_dir"

    run_swaymsg() {
        local err
        if ! err=$(env SWAYSOCK="$swaysock" swaymsg "$@" 2>&1); then
            echo "run-headless-sway: swaymsg '$*' failed: $err" >&2
            cat "$sway_log" >&2 || true
            stop_process "$sway_pid" "$processes_dir"
            rm -rf "$runtime_dir"
            die "failed to configure headless sway ($*)"
        fi
    }
    run_swaymsg create_output
    run_swaymsg "output HEADLESS-2 transform 90"

    [ -z "$socket_path" ] || { mkdir -p "$(dirname "$socket_path")"; ln -sf "$wayland_sock" "$socket_path"; }
    printf 'pid=%s\nruntime_dir=%s\nwayland_sock=%s\nsocket_path=%s\n' \
        "$sway_pid" "$runtime_dir" "$wayland_sock" "$socket_path" > "$processes_dir/$sway_pid.sway-state"
    started_pid=$sway_pid
    started_wayland_sock=$wayland_sock
    return 0
}

stop_sway() {
    local target_pid=${1:-}
    if [ -n "$target_pid" ] && [ -f "$processes_dir/$target_pid.sway-state" ]; then
        _stop_sway_state "$processes_dir/$target_pid.sway-state"
    else
        for sf in "$processes_dir"/*.sway-state; do
            [ -f "$sf" ] || continue
            if [ -n "$socket_path" ]; then
                [ "$(sed -n 's/^socket_path=//p' "$sf")" = "$socket_path" ] && _stop_sway_state "$sf"
            else
                _stop_sway_state "$sf"; break
            fi
        done
    fi
    [ -z "$socket_path" ] || rm -f "$socket_path"
    [ -z "$target_pid" ] || stop_process "$target_pid" "$processes_dir"
    return 0
}

cmd=${1:-}
[ $# -gt 0 ] && shift

case "$cmd" in
    start) parse_options "$@"; start_sway; echo "Started headless Sway [PID: $started_pid] (socket: ${socket_path:-$started_wayland_sock})" ;;
    stop) parse_options "$@"; stop_sway; echo "Stopped headless Sway" ;;
    exec|*)
        [ "$cmd" != "exec" ] && set -- "$cmd" "$@"
        parse_options "$@"
        [ ${#remaining_cmd[@]} -gt 0 ] || die "Usage: run-headless-sway.sh start|stop|[exec] [options...] [--] COMMAND [ARGS...]"
        start_sway
        trap 'stop_sway "$started_pid"' EXIT INT TERM
        env WAYLAND_DISPLAY="${socket_path:-$started_wayland_sock}" "${remaining_cmd[@]}"
        ;;
esac
