<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# src/ui — local rules

`ui` holds the host-agnostic view objects: canvases, chrome, tool handlers and the canvas text control
(`docs/spec/02-modules-and-interfaces.md` §7, `docs/spec/04-capture-and-overlay.md` §5).

- **QtGui only.** No QtWidgets, QtQuick, Qt private API or platform headers; include only `core/`, `render/`,
  `platform/` and `ui/` headers. A view object is shown through a surface host or an ordinary window that embeds a
  `ViewRoot`, never through a widget.
- **Hosts see `platform::SurfaceContent` only.** `ViewRoot` implements it; no host type appears in `ui`, and no code
  here asks which desktop it runs on.
- **One layer per view object.** `ViewObject::paint` draws into the chrome layer. Canonical layers come from `render/`
  and reach the root through `setCanonicalLayers`, so no view object paints into both and surfaces never re-render
  annotations.
- **Accessible by construction.** Every chrome view object gives `accessibleName()` (through `tr()`) and
  `accessibleRole()`; a view with more to expose, such as text or a value, overrides `accessible()` and reports
  `accessibleParent()` and `screenRect()`. Qt wants screen coordinates, so a view never reports surface points.
- **Input state follows the root.** A press makes its view the pointer owner until every button is released, and a
  view that answers the input method's queries is the input method owner (`inputMethodQuery()`); neither is the
  hovered view, which a callback may remove from the tree at any time.
- **One coordinate space.** Geometry, event positions, damage and painting are in the surface's points, top-left
  origin, y down. Damage is rounded outwards to whole points.
- **Tests** run offscreen in `tests/unit/ui/` and assert behaviour. Appearance values are ported from MacShot with
  their `macshot/<path>:<line>@b4d4f3a` source, never invented here.
