#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Runs one case of scripts/check-repo.sh in a throwaway repository and prints the checker's output. CTest matches the
# checker's own message. The private paths are assembled at run time so that this file contains none.
#
# Usage: run-check-repo-case.sh CASE
#   staged-path-hidden-by-working-copy  a staged file holds a home path; the working copy was changed back to safe text
#   reference-and-plans-paths           a commit holds a path into the reference checkout and one into the plans
#   clean-tree                          nothing private anywhere
#   untracked-instruction-file          an untracked CLAUDE.md in the working tree

set -uo pipefail

case_name=${1:-}
checker=$(cd -- "$(dirname -- "$0")/../../scripts" && pwd)/check-repo.sh
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
git_quiet() {
    git -c user.name=repo-test -c user.email=repo-test@example.invalid -c commit.gpgsign=false "$@" >/dev/null
}

home_path=$(printf '/%s/%s/%s/private' home secretowner Ref)
reference_path=$(printf '%s/%s' Ref other)
plans_path=$(printf '%s/%s' plans private-note)

cd "$scratch" || exit 2
git_quiet init -b main
printf '# Instructions\n\n## Before editing, read the nested file for that directory\n\n## Rules\n' >AGENTS.md
echo "Public text" >notes.txt
git_quiet add AGENTS.md notes.txt
git_quiet commit -m "initial"

case "$case_name" in
    staged-path-hidden-by-working-copy)
        echo "$home_path" >notes.txt
        git_quiet add notes.txt
        echo "Safe text" >notes.txt
        "$BASH" "$checker" --staged
        ;;
    reference-and-plans-paths)
        echo "$reference_path and $plans_path" >notes.txt
        git_quiet commit -am "add notes"
        "$BASH" "$checker" --tree HEAD
        ;;
    clean-tree)
        "$BASH" "$checker" --tree HEAD
        ;;
    untracked-instruction-file)
        echo "Instructions" >CLAUDE.md
        "$BASH" "$checker" --staged
        ;;
    *)
        echo "run-check-repo-case: unknown case '$case_name'" >&2
        exit 2
        ;;
esac
