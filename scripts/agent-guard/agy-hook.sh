#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# agy (Antigravity CLI) PreToolUse adapter for the agent guard (.agents/hooks.json registers it with matcher "*").
# agy runs the hook with .agents/ as its working directory and the payload on stdin; the hook answers on stdout with
# {"decision":"deny","reason":...} or {"decision":"allow"}.
#
#   run_command                  toolCall.args.CommandLine is checked as a command, relative to toolCall.args.Cwd;
#   view_*, list_*, read_*, ...  read-only tools are allowed;
#   any other tool               every argument whose name ends in File, Path or Directory (write_to_file and
#                                replace_file_content: TargetFile) is checked as a written path.
# Relative paths resolve against toolCall.args.Cwd, else the first workspace path, else the repository root.
#
# Without jq, or on a bash older than 4, the policy cannot run: commands that push or reset --hard are denied and
# everything else is allowed with a warning on stderr.

set -uo pipefail

# Pure bash, so the fallback below works with an almost empty PATH.
case "${BASH_SOURCE[0]}" in
    */*) here=${BASH_SOURCE[0]%/*} ;;
    *) here=. ;;
esac
here=$(cd -- "$here" && pwd)
payload=""
IFS= read -r -d '' payload || true

allow() {
    printf '{"decision":"allow"}\n'
    exit 0
}

if ! command -v jq >/dev/null 2>&1 || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
    if [[ $payload =~ push|reset[[:space:]]+--hard ]]; then
        printf '{"decision":"deny","reason":"agent-guard: denied [jq-missing] the guard needs jq and bash 4; until then commands that push or reset --hard are blocked"}\n'
        exit 0
    fi
    echo "agent-guard: warning: jq or bash 4 is missing; only push and reset --hard are blocked" >&2
    allow
fi

deny() {
    jq -cn --arg reason "${1:-agent-guard: denied [guard-error] the guard could not evaluate this call}" \
        '{decision: "deny", reason: $reason}'
    exit 0
}

name=$(jq -r '.toolCall.name // empty' <<<"$payload")
cwd=$(jq -r '.toolCall.args.Cwd // .workspacePaths[0] // empty' <<<"$payload")
[ -n "$cwd" ] || cwd=$(cd -- "$here/../.." && pwd)

case "$name" in
    run_command)
        command_text=$(jq -r '.toolCall.args.CommandLine // empty' <<<"$payload")
        result=$("$here/check-command.sh" --cwd "$cwd" --command "$command_text") || deny "$result"
        allow
        ;;
    view_* | list_* | read_* | grep_search | find_by_name | codebase_search | search_web)
        allow
        ;;
    *)
        while IFS= read -r path; do
            [ -n "$path" ] || continue
            result=$("$here/check-command.sh" --cwd "$cwd" --path "$path") || deny "$result"
        done < <(jq -r '(.toolCall.args // {}) | to_entries[]
            | select(.key | test("(File|Path|Directory)s?$"))
            | .value | if type == "array" then .[] else . end | strings' <<<"$payload")
        allow
        ;;
esac
