#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Checks commit messages and pull request titles and bodies against the project's commit rules
# (CONTRIBUTING.md, docs/dev/agent-workflow.md):
#   - Conventional Commits 1.0.0 header: <type>(<scope>)?!?: <subject>, with the project's types and scopes;
#   - header at most 100 characters (pull request titles at most 92, so GitHub's " (#1234)" suffix fits);
#   - subject starts lower-case (mixed-case identifiers such as QPainter or PipeWire are fine) and has no final period;
#   - a blank line between the header and the body;
#   - no references to AI models, agents or tools, and no AI attribution;
#   - no plan, ledger, deviation, gate or milestone identifiers.
#
# Usage:
#   check-commit-message.sh FILE                 a commit message (the commit-msg hook passes .git/COMMIT_EDITMSG)
#   check-commit-message.sh --pr-title FILE      a pull request title
#   check-commit-message.sh --pr-body FILE       a pull request body: attribution rules only
#   check-commit-message.sh --range BASE..HEAD   every non-merge commit in the range
# Exit status 0 when every message passes, 1 when one is rejected, 2 on a usage error. Each rejection prints
# "check-commit-message: rejected [<rule>] <explanation>".

set -uo pipefail

if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
    echo "check-commit-message: bash 4 or newer is required" >&2
    exit 2
fi

readonly TYPES='feat|fix|perf|refactor|test|docs|build|ci|chore|revert'
readonly SCOPES='core|render|ui|hosts|windows|platform|wayland|portal|x11|macos|media|ml|net|app|cli|parity|tests|build|ci|docs|packaging|i18n|deps|repo|release'
# Word boundaries spelled out, because \b is not portable to the BSD regex library on macOS.
readonly B='(^|[^[:alnum:]_])'
readonly E='([^[:alnum:]_]|$)'
# AI models, vendors, agents and tools, as whole words.
readonly AI_PATTERN="${B}(ai|llms?|claude|anthropic|openai|chatgpt|gpt(-?[0-9][a-z0-9.]*)?|codex|gemini|bard|copilot|antigravity|agy|windsurf|aider|grok|deepseek|qwen|mistral|llama|sonnet|opus|haiku)${E}"
readonly ATTRIBUTION_PATTERN='(^co-authored-by:.*(claude|anthropic|openai|gpt|codex|gemini|copilot|noreply@anthropic\.com|\[bot\]))|generated (with|by) |🤖'
# Parity rows, deviations, gates, milestones, phases and dated plan names.
readonly ID_PATTERN="${B}((SH|OV|TB|AN|CR|VE|ED|SV|ST)-[0-9]{2}[a-z]?|DEV-[0-9]{2,3}|G[1-8]|M[0-5]|[Pp]hase[ -]?[0-9]+|[0-9]{6}-[0-9]{4})${E}"

rejected=0

reject() {
    printf 'check-commit-message: rejected [%s] %s\n' "$1" "$2"
    rejected=1
}

# Strips the boundary characters that a spelled-out word boundary includes in a match.
trim_boundary() {
    sed -e 's/^[^[:alnum:]_]*//' -e 's/[^[:alnum:]_.]*$//'
}

# Removes comment lines and everything below git's scissors line.
clean_message() {
    sed -e '/^# -\{8,\} >8 -\{8,\}$/,$d' -e '/^#/d' "$1"
}

check_attribution() {
    local text=$1 label=$2
    if grep -qiE "$ATTRIBUTION_PATTERN" <<<"$text"; then
        reject attribution "$label carries AI attribution (Co-Authored-By an AI, \"Generated with\" or a robot marker)"
    fi
}

check_header() {
    local header=$1 limit=$2 label=$3
    if [ ${#header} -gt "$limit" ]; then
        reject header-length "$label is ${#header} characters; the limit is $limit"
    fi
    if [[ ! $header =~ ^($TYPES)(\(([a-z0-9-]+)\))?(!)?:\ (.+)$ ]]; then
        reject header-format "$label must look like '<type>(<scope>): <subject>' with type one of ${TYPES//|/, }"
        return
    fi
    local scope=${BASH_REMATCH[3]} subject=${BASH_REMATCH[5]}
    if [ -n "$scope" ] && [[ ! $scope =~ ^($SCOPES)$ ]]; then
        reject scope "scope '$scope' is not one of ${SCOPES//|/, }"
    fi
    # Sentence case ("Add tray") is rejected; identifiers such as QPainter, PipeWire or API are allowed.
    if [[ $subject =~ ^[A-Z][a-z]*([^A-Za-z0-9_]|$) ]]; then
        reject subject-case "subject must start lower-case: '$subject'"
    fi
    if [[ $subject == *. ]]; then
        reject subject-period "subject must not end with a period"
    fi
}

check_content() {
    local text=$1 label=$2 match
    match=$(grep -oiE "$AI_PATTERN" <<<"$text" | head -n 1 | trim_boundary)
    if [ -n "$match" ]; then
        reject ai-reference "$label mentions '$match'; commit messages carry no references to AI models, agents or tools"
    fi
    match=$(grep -oE "$ID_PATTERN" <<<"$text" | head -n 1 | trim_boundary)
    if [ -n "$match" ]; then
        reject plan-id "$label contains the identifier '$match'; plan, ledger, gate and milestone IDs stay in issues and pull requests"
    fi
}

check_commit_message() {
    local text=$1 label=$2
    if [ -z "${text//[[:space:]]/}" ]; then
        reject empty "$label is empty"
        return
    fi
    local header second
    header=$(head -n 1 <<<"$text")
    second=$(sed -n 2p <<<"$text")
    check_header "$header" 100 "$label header"
    if [ -n "$second" ]; then
        reject body-separator "$label needs a blank line between the header and the body"
    fi
    check_content "$text" "$label"
    check_attribution "$text" "$label"
}

read_file() {
    if [ ! -r "$1" ]; then
        echo "check-commit-message: cannot read '$1'" >&2
        exit 2
    fi
}

case "${1:-}" in
    --pr-title)
        [ $# -eq 2 ] || { echo "usage: $0 --pr-title FILE" >&2; exit 2; }
        read_file "$2"
        title=$(cat "$2")
        if [ "$(wc -l <<<"$title")" -gt 1 ]; then
            reject header-format "pull request title must be a single line"
        fi
        check_header "$title" 92 "pull request title"
        check_content "$title" "pull request title"
        check_attribution "$title" "pull request title"
        ;;
    --pr-body)
        [ $# -eq 2 ] || { echo "usage: $0 --pr-body FILE" >&2; exit 2; }
        read_file "$2"
        check_attribution "$(cat "$2")" "pull request body"
        ;;
    --range)
        [ $# -eq 2 ] || { echo "usage: $0 --range BASE..HEAD" >&2; exit 2; }
        base=${2%%..*}
        head=${2##*..}
        if [ -z "$base" ] || [ -z "$head" ] || [ "$2" = "$base" ] ||
            ! git cat-file -e "$base^{commit}" 2>/dev/null || ! git cat-file -e "$head^{commit}" 2>/dev/null; then
            echo "check-commit-message: cannot resolve the range '$2'" >&2
            exit 2
        fi
        count=0
        while read -r commit; do
            count=$((count + 1))
            check_commit_message "$(git log -1 --format=%B "$commit")" "commit ${commit:0:12}"
        done < <(git rev-list --no-merges --reverse "$base..$head")
        echo "check-commit-message: checked $count commit(s) in $2"
        ;;
    -*|'')
        echo "usage: $0 FILE | --pr-title FILE | --pr-body FILE | --range BASE..HEAD" >&2
        exit 2
        ;;
    *)
        [ $# -eq 1 ] || { echo "usage: $0 FILE" >&2; exit 2; }
        read_file "$1"
        check_commit_message "$(clean_message "$1")" "commit message"
        ;;
esac

exit "$rejected"
