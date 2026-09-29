#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Owner-run on the Mac: builds and tests one commit in a temporary worktree of this clone, then prints the report
# template for the pull request (docs/dev/macos-checklist.md). Run it after reading the pull request's diff.
#
# Usage: verify.sh COMMIT
#   COMMIT is the pull request's head commit (a full or abbreviated hash). Qt 6.8 or newer must be findable by CMake,
#   for example with CMAKE_PREFIX_PATH="$(brew --prefix qt)". Needs Homebrew bash (bash 4 or newer), CMake, Ninja and jq.

set -uo pipefail

commit=${1:-}
if [ -z "$commit" ]; then
    echo "usage: $0 COMMIT" >&2
    exit 2
fi
if [ "$(uname -s)" != Darwin ]; then
    echo "verify: this script is for the owner's Mac" >&2
    exit 2
fi

cd "$(git rev-parse --show-toplevel)" || exit 2
git fetch --quiet origin "$commit" 2>/dev/null || git fetch --quiet origin
sha=$(git rev-parse --verify "$commit^{commit}" 2>/dev/null) || { echo "verify: unknown commit $commit" >&2; exit 2; }

scratch=$(mktemp -d "${TMPDIR:-/tmp}/ariadshot-verify.XXXXXX")
worktree=$scratch/source
# shellcheck disable=SC2329 # invoked by the EXIT trap
cleanup() {
    git worktree remove --force "$worktree" >/dev/null 2>&1
    rm -rf "$scratch"
}
trap cleanup EXIT

git worktree add --quiet --detach "$worktree" "$sha" || exit 1
log=$scratch/workflow.log
echo "verify: building and testing ${sha:0:12} (log: $log)"
(cd "$worktree" && cmake --workflow --preset dev) >"$log" 2>&1
status=$?
tail -n 20 "$log"

tests_line=$(grep -E 'tests passed|tests failed' "$log" | tail -n 1)
toolchain=$(sed -nE 's/^-- AriadShot toolchain: //p' "$log" | head -n 1)
result=PASS
[ "$status" = 0 ] || result=FAIL

cat <<EOF

---- paste into the pull request ----
### macOS verification

| Item | Value |
| :--- | :--- |
| Commit | \`$sha\` |
| Result | **$result** (cmake --workflow --preset dev exit status $status) |
| Tests | ${tests_line:-no test summary} |
| macOS | $(sw_vers -productVersion) ($(sw_vers -buildVersion)) |
| Hardware | $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m) |
| Toolchain | ${toolchain:-unknown} |

Manual checklist for this change (docs/dev/macos-checklist.md): list each item checked and its result.
---- end ----
EOF
exit "$status"
