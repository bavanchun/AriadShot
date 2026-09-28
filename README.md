<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# AriadShot

AriadShot is a screenshot, annotation and screen-recording tool for Linux and macOS that reproduces the features, look
and feel of [MacShot](https://github.com/sw33tLie/macshot) as faithfully as possible. It is written in C++20 with Qt 6.
Hyprland is the reference Linux desktop; macOS, KDE Plasma, other wlroots compositors and X11 follow, and GNOME is best
effort.

## Status

Pre-alpha, milestone M0 (foundations and go/no-go gates). This repository holds the project skeleton: a tray-only
daemon, the build and test system, the checks and the specification. There is no usable release yet. The milestone
plan is in [`docs/spec/14-gates-and-milestones.md`](docs/spec/14-gates-and-milestones.md).

## Credit and licence

AriadShot is a remake of **MacShot** by sw33tLie and the MacShot contributors
(<https://github.com/sw33tLie/macshot>). Parity is measured against MacShot at commit
[`b4d4f3a`](https://github.com/sw33tLie/macshot/tree/b4d4f3a). MacShot is licensed under the GNU GPL version 3 without
an "or later" option, so AriadShot is licensed **GPL-3.0-only** ([`LICENSE`](LICENSE)). Material reused from MacShot is
listed in [`PROVENANCE.md`](PROVENANCE.md). AriadShot does not use MacShot's name, logo or artwork.

## Build

You need CMake 3.28 or newer, Ninja, a C++20 compiler and Qt 6.8 or newer (Core, Gui, Widgets, Test). On Arch Linux
these are `cmake ninja gcc qt6-base`.

```sh
cmake --workflow --preset dev        # configure, build and run the offscreen test suite
build/dev/src/app/ariadshot-daemon   # start the tray daemon; Quit in the tray menu or Ctrl+C ends it
```

[`docs/dev/building.md`](docs/dev/building.md) covers every preset, macOS, sanitizers and the local checks.

## Documentation

- [`docs/prd.md`](docs/prd.md): product requirements.
- [`docs/spec/`](docs/spec/README.md): technical specification.
- [`docs/adr/`](docs/README.md#decisions): architecture and workflow decisions.
- [`docs/dev/`](docs/README.md#contributor-guides): building, coding standards, testing, workflow and review.
- [`CONTRIBUTING.md`](CONTRIBUTING.md), [`SECURITY.md`](SECURITY.md), [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
