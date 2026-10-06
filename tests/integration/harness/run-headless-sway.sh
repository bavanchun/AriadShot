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
    while [ $# -gt 0 ]; do
        case "$1" in
            --socket) [ $# -ge 2 ] || die "--socket requires an argument"; socket_path=$2; shift 2 ;;
            --processes-dir) [ $# -ge 2 ] || die "--processes-dir requires an argument"; processes_dir=$2; shift 2 ;;
            --) shift; break ;;
            *) break ;;
        esac
    done
    export socket_path processes_dir
}

start_sway() {
    mkdir -p "$processes_dir"
    local runtime_dir
    runtime_dir=$(mktemp -d -t ariad-sway-XXXXXX)
    chmod 0700 "$runtime_dir"
    local sway_log="$runtime_dir/sway.log"

    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u DISPLAY -u SWAYSOCK -u HYPRLAND_INSTANCE_SIGNATURE \
        WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 XDG_RUNTIME_DIR="$runtime_dir" \
        setsid sway -c "$conf_file" > "$sway_log" 2>&1 &
    local sway_pid=$!
    register_process "$sway_pid" "$processes_dir"

    local waited=0 wayland_sock="" swaysock=""
    while [ "$waited" -lt 50 ]; do
        if ! kill -0 "$sway_pid" 2>/dev/null; then
            cat "$sway_log" >&2 || true
            stop_process "$sway_pid" "$processes_dir"
            rm -rf "$runtime_dir"
            die "Sway exited prematurely with code $? (log above)"
        fi
        wayland_sock=$(find "$runtime_dir" -maxdepth 1 -name 'wayland-*' ! -name '*.lock' 2>/dev/null | head -n 1 || true)
        swaysock=$(find "$runtime_dir" -maxdepth 1 -name 'sway-ipc.*.sock' 2>/dev/null | head -n 1 || true)
        [ -n "$wayland_sock" ] && [ -n "$swaysock" ] && [ -S "$wayland_sock" ] && [ -S "$swaysock" ] && break
        sleep 0.1
        waited=$((waited + 1))
    done

    if [ -z "$wayland_sock" ] || [ -z "$swaysock" ]; then
        cat "$sway_log" >&2 || true
        stop_process "$sway_pid" "$processes_dir"
        rm -rf "$runtime_dir"
        die "Timed out waiting for Sway sockets in $runtime_dir"
    fi

    env SWAYSOCK="$swaysock" swaymsg create_output >/dev/null 2>&1 || true
    env SWAYSOCK="$swaysock" swaymsg "output HEADLESS-2 transform 90" >/dev/null 2>&1 || true

    if [ -n "$socket_path" ]; then
        mkdir -p "$(dirname "$socket_path")"
        ln -sf "$wayland_sock" "$socket_path"
    fi

    printf 'pid=%s\nruntime_dir=%s\nwayland_sock=%s\nsocket_path=%s\n' \
        "$sway_pid" "$runtime_dir" "$wayland_sock" "$socket_path" > "$processes_dir/$sway_pid.sway-state"
    started_pid=$sway_pid
    started_wayland_sock=$wayland_sock
    return 0
}

stop_sway() {
    local target_pid=${1:-} state_file=""
    if [ -n "$target_pid" ] && [ -f "$processes_dir/$target_pid.sway-state" ]; then
        state_file="$processes_dir/$target_pid.sway-state"
    else
        for sf in "$processes_dir"/*.sway-state; do
            [ -f "$sf" ] || continue
            if [ -n "$socket_path" ]; then
                if [ "$(sed -n 's/^socket_path=//p' "$sf")" = "$socket_path" ]; then
                    state_file=$sf
                    break
                fi
            else
                state_file=$sf
                break
            fi
        done
    fi

    if [ -n "$state_file" ] && [ -f "$state_file" ]; then
        local pid runtime_dir sf_socket_path
        pid=$(sed -n 's/^pid=//p' "$state_file")
        runtime_dir=$(sed -n 's/^runtime_dir=//p' "$state_file")
        sf_socket_path=$(sed -n 's/^socket_path=//p' "$state_file")
        [ -z "$pid" ] || stop_process "$pid" "$processes_dir"
        [ -z "$sf_socket_path" ] || rm -f "$sf_socket_path"
        [ -z "$runtime_dir" ] || rm -rf "$runtime_dir"
        rm -f "$state_file"
    fi
    [ -z "$socket_path" ] || rm -f "$socket_path"
    [ -z "$target_pid" ] || stop_process "$target_pid" "$processes_dir"
    return 0
}

cmd=${1:-}
[ $# -gt 0 ] && shift

case "$cmd" in
    start)
        parse_options "$@"
        start_sway
        echo "Started headless Sway [PID: $started_pid] (socket: ${socket_path:-$started_wayland_sock})"
        ;;
    stop)
        parse_options "$@"
        stop_sway
        echo "Stopped headless Sway"
        ;;
    exec|*)
        [ "$cmd" != "exec" ] && set -- "$cmd" "$@"
        [ $# -gt 0 ] || die "Usage: run-headless-sway.sh start|stop|[exec] [options...] [--] COMMAND [ARGS...]"
        parse_options "$@"
        start_sway
        target_display=${socket_path:-$started_wayland_sock}
        trap 'stop_sway "$started_pid"' EXIT INT TERM
        env WAYLAND_DISPLAY="$target_display" "$@"
        ;;
esac
