#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# The decision of the owner gate's "inspect" job (.github/workflows/owner-gate.yml): a pull request from a fork that
# changes anything under .github/workflows/ must not reach the owner's approval, because its changed workflows could
# run under a check name the ruleset requires. Such a pull request fails the gate; the owner never approves its
# workflow runs and re-creates the wanted change on a maintainer branch.
#
# Usage: gate-inspect.sh --event EVENT_JSON --files FILES_JSON
#   EVENT_JSON  the pull_request_target event payload ($GITHUB_EVENT_PATH)
#   FILES_JSON  the pull request's changed files from GET /repos/{owner}/{repo}/pulls/{number}/files, every page
#               merged into one JSON array
# Prints "gate-inspect: pass" (exit 0) or "gate-inspect: fail: <reason>" (exit 1). Unreadable or incomplete input
# fails closed.

set -uo pipefail

event=""
files=""
while [ $# -gt 0 ]; do
    case "$1" in
        --event) event=${2-}; shift 2 ;;
        --files) files=${2-}; shift 2 ;;
        *) echo "usage: $0 --event EVENT_JSON --files FILES_JSON" >&2; exit 2 ;;
    esac
done

fail() {
    printf 'gate-inspect: fail: %s\n' "$1"
    exit 1
}

[ -r "$event" ] || fail "cannot read the event payload '$event'"
[ -r "$files" ] || fail "cannot read the changed-file list '$files'"

base_repo=$(jq -er '.pull_request.base.repo.full_name' "$event" 2>/dev/null) ||
    fail "the event payload has no base repository"
# A deleted fork has no head repository; it counts as a fork.
head_repo=$(jq -r '.pull_request.head.repo.full_name // ""' "$event" 2>/dev/null) ||
    fail "the event payload cannot be parsed"
expected_count=$(jq -er '.pull_request.changed_files' "$event" 2>/dev/null) ||
    fail "the event payload has no changed-file count"

jq -e 'type == "array" and all(.[]; (.filename | type) == "string")' "$files" >/dev/null 2>&1 ||
    fail "the changed-file list is not an array of files"
count=$(jq 'length' "$files")
if [ "$count" != "$expected_count" ]; then
    fail "the changed-file list has $count entries but the pull request changes $expected_count files"
fi

workflow_file=$(jq -r '[.[] | .filename, (.previous_filename // empty)
    | select(startswith(".github/workflows/"))] | first // empty' "$files")

if [ "$head_repo" != "$base_repo" ] && [ -n "$workflow_file" ]; then
    fail "fork pull request changes workflows ($workflow_file): do not approve its workflow runs; re-create the change on a maintainer branch"
fi
echo "gate-inspect: pass"
