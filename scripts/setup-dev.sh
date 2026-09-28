#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Prepares a clone for development: activates the git hooks in .githooks/ (git config core.hooksPath, shared by every
# worktree of the clone) and checks the tools that building, testing and scripts/check-all.sh need. It prints what is
# missing with the package names for Arch Linux and Homebrew; it never installs anything.
#
# Usage: setup-dev.sh [--check]   (--check only reports; it does not touch the git configuration)

set -uo pipefail

check_only=0
[ "${1:-}" = --check ] && check_only=1

cd "$(git rev-parse --show-toplevel)" || exit 1
missing=()
problems=0

# have NAME ARCH_PACKAGE BREW_PACKAGE PURPOSE
have() {
    if command -v "$1" >/dev/null 2>&1; then
        printf '  %-14s %s\n' "$1" "$("$1" --version 2>&1 | head -n 1)"
    else
        printf '  %-14s MISSING (%s)\n' "$1" "$4"
        missing+=("$2|$3")
        problems=1
    fi
}

version_at_least() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" = "$2" ]
}

echo "Build and test:"
have cmake cmake cmake "CMake 3.28 or newer"
have ninja ninja ninja "the Ninja generator"
have c++ gcc - "a C++20 compiler; on macOS: xcode-select --install"
have clang++ clang llvm "the dev-clang, asan and tsan presets"
have git git git "version strings and scripts"
have jq jq jq "the agent guard and its tests"
if command -v cmake >/dev/null 2>&1; then
    cmake_version=$(cmake --version | sed -nE 's/^cmake version ([0-9.]+).*/\1/p')
    version_at_least "$cmake_version" 3.28 || { echo "  cmake $cmake_version is older than 3.28"; problems=1; }
fi
qt_version=""
for tool in qtpaths6 qtpaths qmake6 qmake; do
    if command -v "$tool" >/dev/null 2>&1; then
        query=-query
        [[ $tool == qtpaths* ]] && query=--query
        qt_version=$("$tool" "$query" QT_VERSION 2>/dev/null) && break
    fi
done
if [ -n "$qt_version" ]; then
    printf '  %-14s %s\n' Qt "$qt_version"
    version_at_least "$qt_version" 6.8 || { echo "  Qt $qt_version is older than 6.8"; problems=1; }
else
    printf '  %-14s MISSING (Qt 6.8 or newer: Core, Gui, Widgets, Test)\n' Qt
    missing+=("qt6-base|qt")
    problems=1
fi
if [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
    echo "  bash $BASH_VERSION is older than 4; the repository scripts need bash 4 or newer"
    missing+=("bash|bash")
    problems=1
fi

echo "Checks (scripts/check-all.sh):"
have clang-format clang clang-format "formatting; CI pins 22.1.8"
if command -v clang-format >/dev/null 2>&1; then
    format_major=$(clang-format --version | sed -nE 's/.*version ([0-9]+)\..*/\1/p')
    [ "$format_major" = 22 ] || { echo "  clang-format $format_major is not 22.x; install 22.1.8 (pip install clang-format==22.1.8)"; problems=1; }
fi
have shellcheck shellcheck shellcheck "shell script lint"
have actionlint actionlint actionlint "workflow lint"
have reuse reuse reuse "licence headers"
have gitleaks gitleaks gitleaks "secret scanning in hooks and checks"

echo "Optional:"
for tool in ccache clazy-standalone clang-tidy; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '  %-14s found\n' "$tool"
    else
        printf '  %-14s not found (optional)\n' "$tool"
    fi
done

if [ ${#missing[@]} -gt 0 ]; then
    arch_packages=""
    brew_packages=""
    for entry in "${missing[@]}"; do
        arch_packages+=" ${entry%%|*}"
        [ "${entry#*|}" = - ] || brew_packages+=" ${entry#*|}"
    done
    echo
    echo "Missing packages. Install them yourself (after a full system upgrade on Arch):"
    echo "  Arch Linux: sudo pacman -S --needed$arch_packages"
    echo "  macOS:      brew install$brew_packages"
fi

if [ "$check_only" = 0 ]; then
    git config core.hooksPath .githooks
    echo
    echo "Git hooks activated: core.hooksPath = .githooks"
fi
exit "$problems"
