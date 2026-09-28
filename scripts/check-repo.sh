#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Repository hygiene for a public repository worked on by several agent runtimes:
#   1. No per-runtime instruction files: CLAUDE.md, CLAUDE.local.md, .claude/CLAUDE.md, GEMINI.md or Cursor rules,
#      tracked or not. One of them anywhere in the working tree switches Claude Code to CLAUDE.md-only mode, which stops
#      the AGENTS.md files from loading. AGENTS.md is the single instruction file.
#   2. No private paths in the published content or in the published file paths: no home directory, no path into the
#      maintainer's private workspace (its MacShot reference checkout, its plans and reports, its environment file).
#   3. Every nested AGENTS.md is listed in the root AGENTS.md under "Before editing, read the nested file for that
#      directory", and every listed file exists: Codex and agy load nested files only through that list.
#   4. The owner gate workflow embeds the current scripts/github/gate-inspect.sh.
#
# Usage: check-repo.sh [--staged | --tree REV]
#   Checks 2 to 4 read exactly what is published: the index by default and with --staged (the pre-commit hook), or the
#   tree of commit REV with --tree (CI scans the commit under test). A change that exists only in the working copy is
#   not published and is not checked; a staged change hidden by a different working copy is. Check 1 looks at the
#   working tree, because that is where agent runtimes load instruction files from.
# The patterns below are written so that this file does not match itself; it is scanned like every other file.

set -uo pipefail

mode=index
rev=""
case "${1:-}" in
    '' | --staged) ;;
    --tree)
        mode=tree
        rev=${2:-}
        [ -n "$rev" ] || { echo "usage: $0 [--staged | --tree REV]" >&2; exit 2; }
        ;;
    *) echo "usage: $0 [--staged | --tree REV]" >&2; exit 2 ;;
esac

top=$(git rev-parse --show-toplevel) || exit 2
cd "$top" || exit 2
if [ "$mode" = tree ] && ! git rev-parse --quiet --verify "$rev^{commit}" >/dev/null; then
    echo "check-repo: cannot resolve the commit '$rev'" >&2
    exit 2
fi
failed=0

fail() {
    echo "check-repo: $1"
    failed=1
}

# Published file names and contents, from the index or from the tree of $rev.
published_files() {
    if [ "$mode" = tree ]; then
        git -c core.quotePath=false ls-tree -r --name-only "$rev"
    else
        git -c core.quotePath=false ls-files --cached
    fi
}

published_content() {
    if [ "$mode" = tree ]; then
        git show "$rev:$1" 2>/dev/null
    else
        git show ":$1" 2>/dev/null
    fi
}

# published_grep OPTIONS PATTERN: git grep over the published content, with "REV:" prefixes removed.
published_grep() {
    local line
    if [ "$mode" = tree ]; then
        git grep "$1" "$2" "$rev" | while IFS= read -r line; do printf '%s\n' "${line#"$rev":}"; done
    else
        git grep --cached "$1" "$2"
    fi
}

# 1. Instruction files for other runtimes, including untracked and ignored ones.
while IFS= read -r path; do
    fail "${path#./}: per-runtime instruction files are not allowed; AGENTS.md is the single instruction file"
done < <(find . \( -name .git -o -path ./build \) -prune -o \
    \( -name CLAUDE.md -o -name CLAUDE.local.md -o -name GEMINI.md -o -name .cursorrules -o -path '*/.cursor/rules' \) \
    -print)

# 2. Private paths: absolute home directories, and any path through the private workspace's reference checkout or
#    plans directory, or its environment file.
readonly W='(^|[^A-Za-z0-9_])'
readonly PRIVATE_PATHS="(^|[^A-Za-z0-9_./-])/(h[o]me|U[s]ers)/[A-Za-z0-9_.-]+|${W}R[e]f/|\.\./R[e]f($|[^A-Za-z0-9_])|${W}p[l]ans/|w[o]rkspace\.env"
while IFS= read -r match; do
    fail "$(cut -c1-200 <<<"$match"): private path; name MacShot by its upstream URL and commit, and keep workspace paths out of the repository"
done < <(published_grep -nIE "$PRIVATE_PATHS")
# Binary files that match: every matching file minus the text files that match.
while IFS= read -r path; do
    fail "$path: binary file containing a private path"
done < <(comm -23 <(published_grep -lE "$PRIVATE_PATHS" | sort) <(published_grep -lIE "$PRIVATE_PATHS" | sort))
# File paths: a file under a private directory publishes that directory by its name alone.
files=$(published_files)
while IFS= read -r path; do
    fail "$path: private file path; keep the private workspace's directories and files out of the repository"
done < <(grep -E "$PRIVATE_PATHS" <<<"$files")

# 3. Nested AGENTS.md files and the root list.
if ! grep -qx 'AGENTS.md' <<<"$files"; then
    fail "AGENTS.md is missing"
else
    listed=$(published_content AGENTS.md |
        awk '/^## Before editing, read the nested file for that directory/ { in_list = 1; next }
             /^## / { in_list = 0 }
             in_list' | grep -oE '[A-Za-z0-9_./-]+/AGENTS\.md' | sort -u)
    present=$(grep -E '/AGENTS\.md$' <<<"$files" | sort -u)
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if ! grep -qxF "$path" <<<"$listed"; then
            fail "$path is not listed in the root AGENTS.md under 'Before editing, read the nested file for that directory'"
        fi
    done <<<"$present"
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        grep -qxF "$path" <<<"$files" || fail "AGENTS.md lists $path, which does not exist"
    done <<<"$listed"
fi

# 4. The owner gate embeds scripts/github/gate-inspect.sh verbatim, so the gate checks out nothing; keep them identical.
gate_workflow=.github/workflows/owner-gate.yml
if grep -qxF "$gate_workflow" <<<"$files"; then
    embedded=$(published_content "$gate_workflow" |
        awk '/<<.GATE_INSPECT.$/ { inside = 1; next } /^ *GATE_INSPECT$/ { inside = 0 } inside' | sed -E 's/^ {10}//')
    if [ "$embedded" != "$(published_content scripts/github/gate-inspect.sh)" ]; then
        fail "$gate_workflow does not embed the current scripts/github/gate-inspect.sh; copy the script into it"
    fi
fi

[ "$failed" = 0 ] && echo "check-repo: no private paths or per-runtime instruction files; AGENTS.md list and owner gate in sync"
exit "$failed"
