#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Pull requests change at most 500 lines, not counting regression goldens (tests/golden/), generated parity ledger
# files (parity/ledger/<family>.jsonl; hand-written manual-*.jsonl files count), vendored code (third_party/) and
# generated code (any generated/ directory). A larger pull request needs the size/exception label, which the owner
# applies.
#
# Usage: check-pr-size.sh --range BASE..HEAD [--labels-json JSON]
#   JSON is the array of label names, for example '["size/exception"]'.

set -uo pipefail

readonly LIMIT=500
readonly EXCLUDED='^(tests/golden/|third_party/|parity/ledger/[a-z]+\.jsonl$)|(^|/)generated/'

range=""
labels='[]'
while [ $# -gt 0 ]; do
    case "$1" in
        --range) range=${2-}; shift 2 ;;
        --labels-json) labels=${2-}; shift 2 ;;
        *) echo "usage: $0 --range BASE..HEAD [--labels-json JSON]" >&2; exit 2 ;;
    esac
done
base=${range%%..*}
head=${range##*..}
if [ -z "$base" ] || [ -z "$head" ] || [ "$range" = "$base" ] ||
    ! git cat-file -e "$base^{commit}" 2>/dev/null || ! git cat-file -e "$head^{commit}" 2>/dev/null; then
    echo "check-pr-size: cannot resolve the range '$range'" >&2
    exit 2
fi

total=0
excluded=0
while IFS=$'\t' read -r added deleted path; do
    # Binary files report "-".
    [ "$added" = - ] && continue
    if [[ $path =~ $EXCLUDED ]]; then
        excluded=$((excluded + added + deleted))
    else
        total=$((total + added + deleted))
    fi
done < <(git diff --numstat --no-renames "$base...$head")

echo "check-pr-size: $total changed line(s) counted, $excluded excluded (limit $LIMIT)"
if [ "$total" -le "$LIMIT" ]; then
    exit 0
fi
if jq -e 'index("size/exception") != null' <<<"$labels" >/dev/null 2>&1; then
    echo "check-pr-size: over the limit, allowed by the size/exception label"
    exit 0
fi
echo "check-pr-size: $total changed lines exceed $LIMIT; split the pull request, or ask the owner for the size/exception label"
exit 1
