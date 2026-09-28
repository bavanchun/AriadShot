<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# src/core — local rules

`core` holds models, value types, non-pixel algorithms, stores and file formats (`docs/spec/02-modules-and-interfaces.md`
§4). It is the module whose tests assert MacShot values most directly.

- **QtCore only.** No `QImage`, `QPainter`, `QColor` pixels, QtGui, QtWidgets, QtQuick, `qpa/`, `private/` or platform
  headers, and no includes from other modules' directories. Pixel-reading algorithms belong in `render/pixel/`.
- **Unset is not the default.** Models and stores keep "never set" apart from "set to the default value": MacShot's
  import means "remove the key, get the default". Defaults come only from the settings registry and are tested against
  MacShot's table, never against themselves.
- **One canonical canvas space**: top-left origin, points, y down. Convert at the edges only, in `core/geometry/`.
- **Caches are never state.** Rendered text, baked censor images and similar caches are regenerable and are not
  persisted as the source of truth.
- **Lenient decoding within MacShot's limits.** Decoders drop a bad element rather than the whole document, and bound
  every size and count by the limits MacShot applies. Parsers of untrusted input get a fuzz target before they merge.
- **Atomic writes** go through the store helpers (write, sync, rename); never write user files in place.
- **Fallible operations** return `ariadshot::Expected<T, E>` with a module error enum; no exceptions across module
  boundaries.
- **Tests** live in `tests/unit/core/`. Label a test `tsan-safe` when it uses no Qt threading, so the ThreadSanitizer
  preset runs it.
