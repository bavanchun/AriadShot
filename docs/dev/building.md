<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Building AriadShot

The executable owners of the build are [`CMakeLists.txt`](../../CMakeLists.txt),
[`CMakePresets.json`](../../CMakePresets.json) and [`cmake/`](../../cmake/); this page explains how to use them. Design
decisions: [`docs/spec/13-build-ci-release.md`](../spec/13-build-ci-release.md).

## Tools

| Purpose | Tools | Arch Linux packages |
| :--- | :--- | :--- |
| Build and test | CMake ≥ 3.28, Ninja, a C++20 compiler (GCC 13+ or Clang), Qt ≥ 6.8 (Core, Gui, Widgets, Test), git, jq | `cmake ninja gcc qt6-base git jq` |
| Sanitizer and Clang presets | Clang with its sanitizer runtimes | `clang` |
| Local checks | clang-format 22 (CI pins 22.1.8), shellcheck, actionlint, reuse, gitleaks | `clang shellcheck actionlint reuse gitleaks` |
| Optional | ccache, clazy, clang-tidy | `ccache clazy` (clang-tidy is part of `clang`) |

`scripts/setup-dev.sh` checks these, prints what is missing for Arch Linux and Homebrew, and activates the git hooks.
It never installs anything. On Arch, install packages only after a full system upgrade (`pacman -Syu`), because Arch
supports only full upgrades. The repository scripts need bash 4 or newer; on macOS install Homebrew's `bash`.

Configure, build and test never download anything: dependencies come from system packages or `third_party/`. Any
download belongs to a separate acquisition step
([`docs/spec/13-build-ci-release.md` §3](../spec/13-build-ci-release.md#3-dependencies)).

## Presets

`cmake --workflow --preset <name>` configures, builds and runs the tests in `build/<name>/`.

| Preset | Compiler | Build type | Notes |
| :--- | :--- | :--- | :--- |
| `dev` | platform default (GCC on Linux, Apple Clang on macOS) | Debug | the everyday preset; exports `compile_commands.json` for clangd (`.clangd` points at `build/dev`) |
| `dev-clang` | Clang | Debug | also used for clang-tidy and clazy |
| `asan` | Clang | Debug | AddressSanitizer and UndefinedBehaviorSanitizer on the whole offscreen suite |
| `tsan` | Clang | Debug | ThreadSanitizer on tests labelled `tsan-safe` |
| `release` | platform default | RelWithDebInfo | Linux hardening flags (`ARIADSHOT_HARDENING`) |
| `ci-dev`, `ci-asan`, `ci-tsan` | as their parent | as their parent | warnings are errors; CI runs exactly these |

The `dev`, `asan` and `release` test presets skip the labels `wayland`, `nested-hyprland` and `live`. Run one test with
`ctest --preset dev -R <name>`, and list them with `ctest --preset dev -N`.

Local settings that must not change outputs for others (a compiler launcher such as ccache, a faster linker, a Qt
prefix) go into your own `CMakeUserPresets.json`, which git ignores, or into environment variables such as
`CMAKE_CXX_COMPILER_LAUNCHER` and `CMAKE_PREFIX_PATH`.

Useful cache options: `ARIADSHOT_WERROR`, `ARIADSHOT_SANITIZE` (`address,undefined` or `thread`),
`ARIADSHOT_HARDENING`, `ARIADSHOT_EXPECT_QT_VERSION` (configure fails unless that Qt version is found; CI sets it
through the environment for the pinned-Qt jobs).

## Running the daemon

```sh
build/dev/src/app/ariadshot-daemon --version
build/dev/src/app/ariadshot-daemon
```

The daemon shows a tray icon when the desktop provides a system tray (on Hyprland through the bar's status notifier
support) and keeps running without one. Quit it from the tray menu, with Ctrl+C or with `kill -TERM`; all three exit
cleanly with status 0.

## macOS

Install Xcode's command line tools, then `brew install cmake ninja qt bash jq`, and point CMake at Homebrew's Qt:

```sh
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --workflow --preset dev
```

The deployment target is macOS 14.0. CI builds with a pinned Qt 6.8.4 instead of Homebrew's, which moves with every Qt
release. The maintainer verifies pull requests on a Mac with `scripts/macos/verify.sh <commit>`.

## Checks before a pull request

```sh
cmake --workflow --preset dev
cmake --workflow --preset asan
scripts/check-all.sh
```

`scripts/check-all.sh` runs, in CI order: formatting, module include rules, the network-free build rule, repository
hygiene, shellcheck, actionlint, `reuse lint`, gitleaks, and the commit messages of your branch. The git hooks run the
fast subset on every commit and push.

## What CI runs

| Job | Where | Command |
| :--- | :--- | :--- |
| `lint` | Arch Linux container | `scripts/check-all.sh --lint-only` over the pull request's commit range, taken from the base commit |
| `commit-policy` | Ubuntu | pull request title and body, commit messages, pull request size, DCO, with the base commit's checkers |
| `linux-gcc` | Arch Linux container | `cmake --workflow --preset ci-dev` |
| `linux-clang` | Arch Linux container | `ci-asan` and `ci-tsan` workflows, then clang-tidy and clazy on tracked sources |
| `linux-qt-floor` | Ubuntu 24.04, GCC 13, Qt 6.8.4 | `cmake --workflow --preset ci-dev` |
| `macos` | macOS 26, Qt 6.8.4 | `cmake --workflow --preset ci-dev` |
| `macos-14` | macOS 14, Qt 6.8.4 | `cmake --workflow --preset ci-dev` (until GitHub retires the image) |

Each build job writes the Qt, compiler and CMake versions it used to the job summary. `lint` and `commit-policy` run
the checkers of the pull request's base commit, so a change to a checker applies from the next pull request on
([Trusted policy checks](agent-workflow.md#trusted-policy-checks)). To run a checker version against your branch
yourself, call it from another checkout: `/path/to/other/scripts/check-all.sh` checks the current directory.
