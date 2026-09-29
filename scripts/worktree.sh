#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Task worktrees for agents (docs/dev/agent-workflow.md). Used by the manager; Linux only (it reads /proc).
#
#   worktree.sh create <branch> [--agent NAME] [--packet PATH]
#       Fetches origin/main (serialised between concurrent callers), adds a worktree for a new <branch> at
#       $ARIADSHOT_WORKTREES_DIR/<branch with / replaced by ->, default ../worktrees next to the main checkout, records
#       the owner in <worktree>/.agent/owner and prints how to open the agent pane.
#   worktree.sh shell <branch> [COMMAND...]
#       Runs COMMAND (default: an interactive bash) at the worktree root in a clean environment: only HOME, PATH, TERM,
#       LANG, the four ARIADSHOT_* variables and GH_CONFIG_DIR=$ARIADSHOT_WORKSPACE/.agent-gh (the agent token). No
#       shell profile, aliases or functions are loaded, so a runtime name cannot pick up a wrapper that disables its
#       sandbox; GH_TOKEN and SSH_AUTH_SOCK are not passed.
#   worktree.sh remove <branch> [--owner-confirmed]
#       Refuses when the worktree has uncommitted changes or commits that are on no remote. Stops the processes
#       recorded in <worktree>/.agent/processes/ (only while their PID, start time and command line still match),
#       lists every other process whose working directory is inside the worktree and refuses while there are any unless
#       the owner confirmed, then runs git worktree remove without --force. It never kills unrecorded processes.
#   worktree.sh list
#
# Process records: one file per process, <worktree>/.agent/processes/<pid>, with the lines "start=<field 22 of
# /proc/<pid>/stat>" and "cmdline=<the NUL-separated command line with NULs replaced by spaces>".

set -uo pipefail

die() {
    echo "worktree: $1" >&2
    exit 1
}

common_dir=$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || die "not inside the AriadShot repository"
main_checkout=${common_dir%/.git}
worktrees_root=${ARIADSHOT_WORKTREES_DIR:-$main_checkout/../worktrees}
if [ -d "$worktrees_root" ]; then
    worktrees_root=$(cd "$worktrees_root" && pwd -P)
fi

worktree_path() {
    printf '%s/%s' "$worktrees_root" "${1//\//-}"
}

process_cmdline() {
    tr '\0' ' ' <"/proc/$1/cmdline" 2>/dev/null | sed 's/ $//'
}

process_start() {
    # Field 22 of /proc/<pid>/stat, counted after the command name, which may contain spaces.
    sed -E 's/^.*\) //' "/proc/$1/stat" 2>/dev/null | cut -d ' ' -f 20
}

record_matches() {
    local pid=$1 start=$2 cmdline=$3
    [ -d "/proc/$pid" ] && [ "$(process_start "$pid")" = "$start" ] && [ "$(process_cmdline "$pid")" = "$cmdline" ]
}

create() {
    local branch=${1:-} agent="" packet=""
    [ -n "$branch" ] || die "usage: worktree.sh create <branch> [--agent NAME] [--packet PATH]"
    shift
    while [ $# -gt 0 ]; do
        case "$1" in
            --agent) agent=${2-}; shift 2 ;;
            --packet) packet=${2-}; shift 2 ;;
            *) die "unknown option $1" ;;
        esac
    done
    git check-ref-format --branch "$branch" >/dev/null 2>&1 || die "'$branch' is not a valid branch name"
    mkdir -p "$worktrees_root" && worktrees_root=$(cd "$worktrees_root" && pwd -P)
    local dir
    dir=$(worktree_path "$branch")
    [ ! -e "$dir" ] || die "$dir already exists"

    # Shared refs are the contention point between parallel creations: serialise the fetch.
    local lock=$common_dir/ariadshot-fetch.lock waited=0
    until mkdir "$lock" 2>/dev/null; do
        [ "$waited" -lt 120 ] || die "another fetch holds $lock; remove it if no fetch is running"
        sleep 1
        waited=$((waited + 1))
    done
    git -C "$main_checkout" fetch --quiet origin main
    local fetched=$?
    rmdir "$lock"
    [ "$fetched" = 0 ] || die "git fetch origin main failed"

    git -C "$main_checkout" worktree add --quiet -b "$branch" "$dir" origin/main || die "git worktree add failed"
    mkdir -p "$dir/.agent/processes"
    {
        echo "branch=$branch"
        echo "agent=$agent"
        echo "packet=$packet"
        echo "created=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } >"$dir/.agent/owner"

    echo "Created $dir on $branch from origin/main."
    echo "Open the agent pane at the worktree root with the clean environment:"
    echo "  $main_checkout/scripts/worktree.sh shell $branch"
    [ -n "${ARIADSHOT_WORKSPACE:-}" ] || echo "Warning: ARIADSHOT_WORKSPACE is not set; source the workspace environment first."
}

shell() {
    local branch=${1:-}
    [ -n "$branch" ] || die "usage: worktree.sh shell <branch> [COMMAND...]"
    shift
    local dir
    dir=$(worktree_path "$branch")
    [ -d "$dir" ] || die "no worktree at $dir"
    [ -n "${ARIADSHOT_WORKSPACE:-}" ] || die "ARIADSHOT_WORKSPACE is not set; source the workspace environment first"
    [ $# -gt 0 ] || set -- bash --noprofile --norc -i
    cd "$dir" || die "cannot enter $dir"
    exec env -i \
        HOME="$HOME" PATH="$PATH" TERM="${TERM:-xterm-256color}" LANG="${LANG:-C.UTF-8}" \
        ARIADSHOT_WORKSPACE="$ARIADSHOT_WORKSPACE" \
        ARIADSHOT_MACSHOT_DIR="${ARIADSHOT_MACSHOT_DIR:-}" \
        ARIADSHOT_PLANS_DIR="${ARIADSHOT_PLANS_DIR:-}" \
        ARIADSHOT_WORKTREES_DIR="$worktrees_root" \
        GH_CONFIG_DIR="$ARIADSHOT_WORKSPACE/.agent-gh" \
        "$@"
}

remove() {
    local branch=${1:-} confirmed=0
    [ -n "$branch" ] || die "usage: worktree.sh remove <branch> [--owner-confirmed]"
    [ "${2:-}" = --owner-confirmed ] && confirmed=1
    local dir
    dir=$(worktree_path "$branch")
    [ -d "$dir" ] || die "no worktree at $dir"

    [ -z "$(git -C "$dir" status --porcelain)" ] || die "$dir has uncommitted changes"
    local unpushed
    if git -C "$dir" rev-parse --quiet --verify '@{upstream}' >/dev/null 2>&1; then
        unpushed=$(git -C "$dir" rev-list --count '@{upstream}..HEAD')
    else
        unpushed=$(git -C "$dir" rev-list --count HEAD --not --remotes)
    fi
    [ "$unpushed" = 0 ] || die "$dir has $unpushed commit(s) that are on no remote"

    local record pid start cmdline
    for record in "$dir"/.agent/processes/*; do
        [ -f "$record" ] || continue
        pid=${record##*/}
        start=$(sed -n 's/^start=//p' "$record")
        cmdline=$(sed -n 's/^cmdline=//p' "$record")
        if record_matches "$pid" "$start" "$cmdline"; then
            echo "Stopping recorded process $pid: $cmdline"
            kill -TERM "$pid" 2>/dev/null
            local waited=0
            while record_matches "$pid" "$start" "$cmdline" && [ "$waited" -lt 10 ]; do
                sleep 1
                waited=$((waited + 1))
            done
            if record_matches "$pid" "$start" "$cmdline"; then
                echo "Process $pid ignored SIGTERM; sending SIGKILL"
                kill -KILL "$pid" 2>/dev/null
            fi
        fi
        rm -f "$record"
    done

    local others="" proc cwd
    for proc in /proc/[0-9]*; do
        pid=${proc#/proc/}
        [ "$pid" = "$$" ] && continue
        cwd=$(readlink "$proc/cwd" 2>/dev/null) || continue
        if [ "$cwd" = "$dir" ] || [[ $cwd == "$dir"/* ]]; then
            others+="  $pid $(process_cmdline "$pid")"$'\n'
        fi
    done
    if [ -n "$others" ]; then
        echo "Processes still working inside $dir (not recorded, so not stopped):"
        printf '%s' "$others"
        [ "$confirmed" = 1 ] || die "stop them or ask the owner, then rerun with --owner-confirmed"
    fi

    git -C "$main_checkout" worktree remove "$dir" || die "git worktree remove refused; inspect $dir"
    echo "Removed $dir. The branch $branch is kept."
}

list() {
    git -C "$main_checkout" worktree list
    local owner_file
    for owner_file in "$worktrees_root"/*/.agent/owner; do
        [ -f "$owner_file" ] || continue
        echo
        echo "${owner_file%/.agent/owner}:"
        sed 's/^/  /' "$owner_file"
    done
}

command=${1:-}
shift || true
case "$command" in
    create) create "$@" ;;
    shell) shell "$@" ;;
    remove) remove "$@" ;;
    list) list ;;
    *) die "usage: worktree.sh create|shell|remove|list ..." ;;
esac
