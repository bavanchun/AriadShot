#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Claude Code PreToolUse adapter for the agent guard (.claude/settings.json registers it for the Bash tool and for
# Edit|Write|MultiEdit|NotebookEdit). It reads the hook payload on stdin, asks check-command.sh, and blocks a denied
# call with exit status 2 and the reason on stderr. Relative paths resolve against the payload's cwd.
#
# Claude loads project settings only when a session starts at the project root, so the guard is active only for
# sessions started at the worktree root (docs/dev/agent-workflow.md).
#
# Without jq, or on a bash older than 4, the policy cannot run: commands that push or reset --hard are denied and
# everything else is allowed with a warning.

set -uo pipefail

# Pure bash, so the fallback below works with an almost empty PATH.
case "${BASH_SOURCE[0]}" in
    */*) here=${BASH_SOURCE[0]%/*} ;;
    *) here=. ;;
esac
here=$(cd -- "$here" && pwd)
payload=""
IFS= read -r -d '' payload || true

if ! command -v jq >/dev/null 2>&1 || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
    if [[ $payload =~ push|reset[[:space:]]+--hard ]]; then
        echo "agent-guard: denied [jq-missing] the guard needs jq and bash 4; until then commands that push or reset --hard are blocked" >&2
        exit 2
    fi
    echo "agent-guard: warning: jq or bash 4 is missing; only push and reset --hard are blocked" >&2
    exit 0
fi

tool=$(jq -r '.tool_name // empty' <<<"$payload")
cwd=$(jq -r '.cwd // empty' <<<"$payload")
[ -n "$cwd" ] || cwd=${CLAUDE_PROJECT_DIR:-$PWD}

case "$tool" in
    Bash)
        command_text=$(jq -r '.tool_input.command // empty' <<<"$payload")
        result=$("$here/check-command.sh" --cwd "$cwd" --command "$command_text")
        ;;
    Edit | Write | MultiEdit | NotebookEdit)
        path=$(jq -r '.tool_input.file_path // .tool_input.notebook_path // empty' <<<"$payload")
        [ -n "$path" ] || exit 0
        result=$("$here/check-command.sh" --cwd "$cwd" --path "$path")
        ;;
    *)
        exit 0
        ;;
esac
status=$?

if [ "$status" -ne 0 ]; then
    printf '%s\n' "${result:-agent-guard: denied [guard-error] the guard could not evaluate this call}" >&2
    exit 2
fi
exit 0
