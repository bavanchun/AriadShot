#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Runs every local check in CI order: the checks of the CI "lint" job, then the commit messages that the
# "commit-policy" job checks. Building and testing are separate: cmake --workflow --preset dev (and asan).
#
# Usage: check-all.sh [--range BASE..HEAD] [--lint-only]
#   --range      the commits to scan for secrets and to check messages of. Without it, gitleaks scans the whole
#                history and the commit messages are those between the merge base with origin/main (or main) and HEAD.
#   --lint-only  only the lint checks (the CI lint job runs this).
# Needs clang-format 22, shellcheck, actionlint, reuse, gitleaks and jq (scripts/setup-dev.sh lists what is missing).
#
# The checkers and the gitleaks configuration are taken from the directory of this script, and the repository checked
# is the current directory. CI uses this to run the base branch's checkers against a pull request's checkout, so a pull
# request cannot weaken the checks that judge it (docs/dev/agent-workflow.md, "Trusted policy checks").

set -uo pipefail

range=""
lint_only=0
while [ $# -gt 0 ]; do
    case "$1" in
        --range) range=${2-}; shift 2 ;;
        --lint-only) lint_only=1; shift ;;
        *) echo "usage: $0 [--range BASE..HEAD] [--lint-only]" >&2; exit 2 ;;
    esac
done

checkers=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$(git rev-parse --show-toplevel)" || exit 2
failed=()

run() {
    local name=$1
    shift
    echo "== $name"
    if ! "$@"; then
        failed+=("$name")
    fi
}

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "check-all: $1 is not installed (see scripts/setup-dev.sh)"
        return 1
    fi
}

shell_scripts() {
    git ls-files -z -- '*.sh' '.githooks/*'
}

run_shellcheck() {
    require shellcheck || return 1
    shell_scripts | xargs -0 shellcheck &&
        # PKGBUILD variables are read by makepkg, and makepkg provides $srcdir and $pkgdir.
        shellcheck --shell=bash --exclude=SC2034,SC2154 packaging/arch/PKGBUILD
}

run_actionlint() {
    require actionlint && actionlint
}

run_reuse() {
    require reuse && reuse lint
}

run_gitleaks() {
    require gitleaks || return 1
    if [ -n "$range" ]; then
        gitleaks git --no-banner --redact --config "$checkers/../.gitleaks.toml" --log-opts="$range" .
    else
        gitleaks git --no-banner --redact --config "$checkers/../.gitleaks.toml" .
    fi
}

run_commit_messages() {
    local commits=$range
    if [ -z "$commits" ]; then
        local base=""
        base=$(git merge-base HEAD origin/main 2>/dev/null || git merge-base HEAD main 2>/dev/null)
        if [ -z "$base" ] || [ "$base" = "$(git rev-parse HEAD)" ]; then
            echo "check-all: no commits on this branch beyond main"
            return 0
        fi
        commits=$base..HEAD
    fi
    "$BASH" "$checkers/check-commit-message.sh" --range "$commits"
}

run format "$BASH" "$checkers/check-format.sh"
run architecture "$BASH" "$checkers/check-architecture.sh"
run no-network-build "$BASH" "$checkers/check-no-network-build.sh"
# The committed tree is what a pull request publishes.
run repository "$BASH" "$checkers/check-repo.sh" --tree HEAD
run shellcheck run_shellcheck
run actionlint run_actionlint
run reuse run_reuse
run gitleaks run_gitleaks
if [ "$lint_only" = 0 ]; then
    run commit-messages run_commit_messages
fi

echo
if [ ${#failed[@]} -gt 0 ]; then
    echo "check-all: FAILED: ${failed[*]}"
    exit 1
fi
echo "check-all: every check passed"
