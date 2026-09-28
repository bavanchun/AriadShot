<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# src/render — local rules

`render` is the one pixel source: canonical images, renderers, the text engine, effects and encoders
(`docs/spec/02-modules-and-interfaces.md` §5, `docs/spec/03-rendering-contracts.md`).

- **Every image comes from `makeCanonicalImage()`**: `ARGB32_Premultiplied`, `QColorSpace::SRgb`, device pixel ratio
  equal to the capture scale, transparent fill. Never create a canvas image any other way.
- **Fixed inputs.** Bundled fonts registered explicitly on Linux, hinting off, no subpixel antialiasing, text laid out
  in canvas points at a fixed logical DPI. Image interpolation per draw call as MacShot does.
- **Presentation never re-renders.** Surfaces display canonical layers through the view transform; they never paint
  annotations themselves.
- **Pixel loops live only in `render/pixel/`**, the audited pixel module, behind bounds-checked `std::span` accessors.
  Code elsewhere calls it and never indexes image memory.
- **QtGui only**: no QtWidgets, QtQuick, Qt private API or platform headers. `scripts/check-architecture.sh` and the
  configure-time link check enforce this.
- **Tests** compare pixels with `tests/support/ImageCompare` (exact or CIEDE2000 class). Golden images are created
  only through `ARIADSHOT_WRITE_GOLDEN_CANDIDATES` and a reviewed `golden-update` pull request.
