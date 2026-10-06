<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# src/platform — local rules

`platform` declares every desktop-dependent service as an abstract interface, with the value types they exchange
(`docs/spec/02-modules-and-interfaces.md` §6). Implementations live in `src/backends/` and `src/hosts/`.

- **Interfaces only.** No implementation of a platform service here, and no Wayland, X11, Cocoa, PipeWire, D-Bus or
  other platform headers; include only QtCore, QtGui, `core/` and `platform/`. `scripts/check-architecture.sh` and the
  configure-time link check enforce this.
- **Value types without platform types.** Parameters and results are Qt value types or small structs of them:
  `OutputId` carries the `wl_output` name, never a `wl_output*`, an `NSScreen*` or an X11 id.
- **`ui` never appears here.** `SurfaceContent` is implemented by `ui::ViewRoot`, and a host depends on this module
  alone; do not include `ui/` headers.
- **Asynchronous operations** return at once and complete once on the GUI thread (spec 02 §6). `SurfaceHost`,
  `HostedSurface` and `SurfaceContent` are GUI-thread objects.
- **Each header states its thread affinity**; a source file exists only for real code, such as the interfaces'
  destructors in `Surfaces.cpp`.
