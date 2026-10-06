<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# src/media — local rules

`media` implements recording, media pipelines, and the studio compositor
(`docs/spec/02-modules-and-interfaces.md` §11, `docs/spec/03-rendering-contracts.md` §3,
`docs/spec/05-recording-and-studio.md`).

- **Studio rendering contract.** Preview and export render the exact same immutable `FrameScene` through
  `StudioCompositor::render()` with identical passes and shaders (`arch §3.4.3`).
- **Working space and precision.** Blending occurs in an RGBA16F linear working target; the final pass
  encodes to 8-bit sRGB (`QColorSpace::SRgb`).
- **Shaders.** Shaders are written in portable GLSL and compiled with `qt_add_shaders`. Shaders stay portable
  across Vulkan, Metal, and OpenGL.
- **API selection.** Linux defaults to Vulkan explicitly (`QRhiWidget::Api::Vulkan`), with OpenGL as fallback.
  Export creates its own offscreen `QRhi` on the same backend and never borrows the widget's `QRhi`.
- **Dependencies.** `core`, `render`, and `Qt6::GuiPrivate`. No QtWidgets in `src/media/` (widgets belong in `ui/`
  or `tools/bench/`).
- **Tests.** Unit tests assert deterministic rendering and exact-or-presentation parity (`tests/support/ImageCompare`).
