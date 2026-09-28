#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Repository hygiene for a public repository worked on by several agent runtimes:
#   1. No per-runtime instruction files: CLAUDE.md, CLAUDE.local.md, .claude/CLAUDE.md, GEMINI.md or Cursor rules, tracked
#      or not. One of them anywhere in the working tree switches Claude Code to CLAUDE.md-only mode, which stops the
#      AGENTS.md files from loading. AGENTS.md is the single instruction file.
#   2. No private paths in tracked files: home directories and the maintainer's private workspace (the MacShot
#      reference checkout and private plans) never appear in the public repository.
#   3. Every nested AGENTS.md is listed in the root AGENTS.md under "Before editing, read the nested file for that
#      directory", and every listed file exists: Codex and agy load nested files only through that list.
#   4. The owner gate workflow embeds the current scripts/github/gate-inspect.sh.
# This file is excluded from check 2, because it names the patterns.

set -uo pipefail

top=$(git rev-parse --show-toplevel) || exit 2
cd "$top" || exit 2
failed=0

fail() {
    echo "check-repo: $1"
    failed=1
}

# 1. Instruction files for other runtimes, including untracked and ignored ones.
while IFS= read -r path; do
    fail "${path#./}: per-runtime instruction files are not allowed; AGENTS.md is the single instruction file"
done < <(find . \( -name .git -o -path ./build \) -prune -o \
    \( -name CLAUDE.md -o -name CLAUDE.local.md -o -name GEMINI.md -o -name .cursorrules -o -path '*/.cursor/rules' \) \
    -print)

# 2. Private paths.
readonly PRIVATE_PATHS='(^|[^A-Za-z0-9_./-])(/home|/Users)/[A-Za-z0-9_.-]+|\.\./Ref([/[:space:]"'"'"'`]|$)|Ref/macshot|plans/reports|workspace\.env'
while IFS= read -r match; do
    fail "$match: private path; name MacShot by its upstream URL and commit, and keep workspace paths out of the repository"
done < <(git grep -nE "$PRIVATE_PATHS" -- ':!scripts/check-repo.sh' | cut -c1-200)

# 3. Nested AGENTS.md files and the root list.
if [ ! -f AGENTS.md ]; then
    fail "AGENTS.md is missing"
else
    listed=$(awk '/^## Before editing, read the nested file for that directory/ { in_list = 1; next }
                  /^## / { in_list = 0 }
                  in_list' AGENTS.md | grep -oE '[A-Za-z0-9_./-]+/AGENTS\.md' | sort -u)
    present=$(git ls-files --cached --others --exclude-standard -- '*AGENTS.md' | grep -v '^AGENTS\.md$' | sort -u)
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if ! grep -qxF "$path" <<<"$listed"; then
            fail "$path is not listed in the root AGENTS.md under 'Before editing, read the nested file for that directory'"
        fi
    done <<<"$present"
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        [ -f "$path" ] || fail "AGENTS.md lists $path, which does not exist"
    done <<<"$listed"
fi

# 4. The owner gate embeds scripts/github/gate-inspect.sh verbatim, so the gate checks out nothing; keep them identical.
gate_workflow=.github/workflows/owner-gate.yml
if [ -f "$gate_workflow" ]; then
    embedded=$(awk '/<<.GATE_INSPECT.$/ { inside = 1; next } /^ *GATE_INSPECT$/ { inside = 0 } inside' "$gate_workflow" |
        sed -E 's/^ {10}//')
    if [ "$embedded" != "$(cat scripts/github/gate-inspect.sh)" ]; then
        fail "$gate_workflow does not embed the current scripts/github/gate-inspect.sh; copy the script into it"
    fi
fi

[ "$failed" = 0 ] && echo "check-repo: no private paths or per-runtime instruction files; AGENTS.md list and owner gate in sync"
exit "$failed"
