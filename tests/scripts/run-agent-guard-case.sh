#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Runs one case of agent-guard-cases.tsv through the Claude or agy adapter, with a payload shaped like the one that
# runtime sends, inside throwaway repositories. Prints the adapter's output and a verdict line
#   guard-case: <name> <runtime> -> allow | deny [<rule>]
# where <rule> is taken from the guard's own message; CTest matches that line. Exits 0 when the verdict equals the
# case's expectation.
#
# Usage: run-agent-guard-case.sh CASES_TSV CASE_NAME claude|agy

set -uo pipefail

if [ $# -ne 3 ]; then
    echo "usage: $0 CASES_TSV CASE_NAME claude|agy" >&2
    exit 2
fi
cases=$1
case_name=$2
runtime=$3
guard_dir=$(cd -- "$(dirname -- "$0")/../../scripts/agent-guard" && pwd)

line=$(awk -F '\t' -v name="$case_name" '$1 == name' "$cases")
if [ -z "$line" ]; then
    echo "run-agent-guard-case: no case named '$case_name' in $cases" >&2
    exit 2
fi
IFS=$'\t' read -r name tools cwd environment expectation input <<<"$line"

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Throwaway repositories, isolated from the user's git configuration.
export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
git_quiet() {
    git -c user.name=guard-test -c user.email=guard-test@example.invalid -c commit.gpgsign=false \
        -c tag.gpgsign=false "$@" >/dev/null
}
mkdir -p "$scratch/work/src" "$scratch/main" "$scratch/reference/macshot"
git_quiet -C "$scratch/work" init -b feat/maintenance
git_quiet -C "$scratch/work" commit --allow-empty -m init
git_quiet -C "$scratch/work" tag v0.1.0
git_quiet -C "$scratch/main" init -b main
git_quiet -C "$scratch/main" commit --allow-empty -m init
echo reference >"$scratch/reference/macshot/README.md"

substitute() {
    local text=$1
    text=${text//@WORK@/$scratch/work}
    text=${text//@MAIN@/$scratch/main}
    text=${text//@MACSHOT@/$scratch/reference/macshot}
    text=${text//@TMP@/$scratch}
    printf '%s' "$text"
}
cwd=$(substitute "$cwd")
input=$(substitute "$input")

macshot_dir=$scratch/reference/macshot
path_value=$PATH
case "$environment" in
    -) ;;
    macshot-empty) macshot_dir="" ;;
    no-jq) mkdir -p "$scratch/empty-bin" && path_value=$scratch/empty-bin ;;
    *) echo "run-agent-guard-case: unknown environment '$environment'" >&2; exit 2 ;;
esac

claude_tool=${tools%%/*}
agy_tool=${tools#*/}
case "$runtime" in
    claude) tool=$claude_tool ;;
    agy) tool=$agy_tool ;;
    *) echo "run-agent-guard-case: unknown runtime '$runtime'" >&2; exit 2 ;;
esac
if [ "$tool" = - ]; then
    echo "run-agent-guard-case: case '$name' has no $runtime tool" >&2
    exit 2
fi

if [ "$runtime" = claude ]; then
    case "$tool" in
        Bash) tool_input=$(jq -cn --arg value "$input" '{command: $value}') ;;
        NotebookEdit) tool_input=$(jq -cn --arg value "$input" '{notebook_path: $value, new_source: ""}') ;;
        *) tool_input=$(jq -cn --arg value "$input" '{file_path: $value}') ;;
    esac
    payload=$(jq -cn --arg cwd "$cwd" --arg tool "$tool" --argjson input "$tool_input" \
        '{session_id: "guard-test", hook_event_name: "PreToolUse", cwd: $cwd, tool_name: $tool, tool_input: $input}')
    output=$(cd "$scratch" && env PATH="$path_value" ARIADSHOT_MACSHOT_DIR="$macshot_dir" CLAUDE_PROJECT_DIR="$cwd" \
        "$BASH" "$guard_dir/claude-hook.sh" <<<"$payload" 2>&1)
    status=$?
    case "$status" in
        0) verdict=allow ;;
        2) verdict=deny ;;
        *) verdict="error (exit $status)" ;;
    esac
    reason=$output
else
    case "$tool" in
        run_command) args=$(jq -cn --arg value "$input" --arg cwd "$cwd" '{CommandLine: $value, Cwd: $cwd}') ;;
        view_file) args=$(jq -cn --arg value "$input" '{AbsolutePath: $value}') ;;
        *) args=$(jq -cn --arg value "$input" '{TargetFile: $value}') ;;
    esac
    payload=$(jq -cn --arg tool "$tool" --arg cwd "$cwd" --argjson args "$args" \
        '{conversationId: "guard-test", stepIdx: 1, workspacePaths: [$cwd], toolCall: {name: $tool, args: $args}}')
    # agy runs hooks from the .agents/ directory next to hooks.json.
    mkdir -p "$scratch/work/.agents"
    output=$(cd "$scratch/work/.agents" && env PATH="$path_value" ARIADSHOT_MACSHOT_DIR="$macshot_dir" \
        "$BASH" "$guard_dir/agy-hook.sh" <<<"$payload" 2>/dev/null)
    status=$?
    decision=$(jq -r '.decision // empty' <<<"$output" 2>/dev/null)
    reason=$(jq -r '.reason // empty' <<<"$output" 2>/dev/null)
    if [ "$status" -ne 0 ] || [ -z "$decision" ]; then
        verdict="error (exit $status, output '$output')"
    else
        verdict=$decision
    fi
fi

printf '%s\n' "$output"
if [ "$verdict" = deny ] && [[ $reason =~ denied\ \[([a-z0-9-]+)\] ]]; then
    verdict="deny [${BASH_REMATCH[1]}]"
fi
echo "guard-case: $name $runtime -> $verdict"

if [ "$expectation" = allow ]; then
    [ "$verdict" = allow ]
else
    [ "$verdict" = "deny [${expectation#deny:}]" ]
fi
