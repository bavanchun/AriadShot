#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Checks C, C++, Objective-C and Objective-C++ formatting against .clang-format. clang-format 22 is required (CI pins
# 22.1.8), because other major versions format differently.
#
# Usage:
#   check-format.sh            every tracked source file
#   check-format.sh --staged   the staged content of staged source files (pre-commit hook)
# Set CLANG_FORMAT to use a specific binary. Exit 1 when a file is not formatted.

set -uo pipefail

readonly REQUIRED_MAJOR=22
readonly PINNED_VERSION=22.1.8
clang_format=${CLANG_FORMAT:-clang-format}

if ! command -v "$clang_format" >/dev/null 2>&1; then
    echo "check-format: $clang_format not found; install clang-format $PINNED_VERSION (see docs/dev/building.md)" >&2
    exit 2
fi
version=$("$clang_format" --version | sed -nE 's/.*version ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p')
if [ "${version%%.*}" != "$REQUIRED_MAJOR" ]; then
    echo "check-format: clang-format $REQUIRED_MAJOR.x is required, found '${version:-unknown}'" >&2
    exit 2
fi
if [ "$version" != "$PINNED_VERSION" ]; then
    echo "check-format: note: clang-format $version; CI uses $PINNED_VERSION" >&2
fi

cd "$(git rev-parse --show-toplevel)" || exit 2
readonly PATTERNS=('*.c' '*.cc' '*.cpp' '*.h' '*.hpp' '*.m' '*.mm')

failed=0
count=0
if [ "${1:-}" = --staged ]; then
    while IFS= read -r -d '' file; do
        count=$((count + 1))
        if ! git show ":$file" | "$clang_format" --assume-filename="$file" --dry-run --Werror >/dev/null 2>&1; then
            echo "check-format: $file is not formatted (run: $clang_format -i $file)"
            failed=1
        fi
    done < <(git diff --cached --name-only --diff-filter=ACMR -z -- "${PATTERNS[@]}")
elif [ $# -eq 0 ]; then
    while IFS= read -r -d '' file; do
        count=$((count + 1))
        if ! "$clang_format" --dry-run --Werror "$file" >/dev/null 2>&1; then
            echo "check-format: $file is not formatted (run: $clang_format -i $file)"
            failed=1
        fi
    done < <(git ls-files -z -- "${PATTERNS[@]}")
else
    echo "usage: $0 [--staged]" >&2
    exit 2
fi

[ "$failed" = 0 ] && echo "check-format: $count file(s) formatted"
exit "$failed"
