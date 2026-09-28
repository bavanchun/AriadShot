#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Configure, build and test fetch nothing (docs/spec/13-build-ci-release.md §3.1). This check rejects, in every
# tracked CMake file, FetchContent, ExternalProject, file(DOWNLOAD|UPLOAD) and commands that call network tools.
# Dependencies come from system packages or third_party/, installed in a separate acquisition step.
#
# This is a policy check, not a guarantee: no job isolates the network yet.

set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 2

readonly B='(^|[^A-Za-z0-9_])'
readonly RULES=(
    "FetchContent|${B}FetchContent"
    "ExternalProject|${B}ExternalProject"
    "file(DOWNLOAD/UPLOAD)|${B}file[[:space:]]*\([[:space:]]*(DOWNLOAD|UPLOAD)"
    "network tool|${B}(curl|wget|aria2c|rsync|scp|ftp)([^A-Za-z0-9_]|$)"
    "git network command|${B}git[[:space:]]+(clone|fetch|pull|ls-remote|submodule|archive[[:space:]]+--remote)"
    "package download|${B}(pip3?[[:space:]]+(install|download)|npm|npx|pnpm|yarn|cargo[[:space:]]+(install|fetch)|go[[:space:]]+(get|mod[[:space:]]+download)|conan|vcpkg)([^A-Za-z0-9_]|$)"
)

found=0
count=0
while IFS= read -r -d '' file; do
    count=$((count + 1))
    for rule in "${RULES[@]}"; do
        label=${rule%%|*}
        pattern=${rule#*|}
        while IFS=: read -r number _; do
            echo "check-no-network-build: $file:$number: $label is not allowed in the build; add the dependency in the acquisition step"
            found=1
        done < <(grep -nE "$pattern" "$file" | grep -vE '^[0-9]+:[[:space:]]*#')
    done
done < <(git ls-files -z -- '*CMakeLists.txt' '*.cmake' '*.cmake.in')

[ "$found" = 0 ] && echo "check-no-network-build: $count CMake file(s) fetch nothing"
exit "$found"
