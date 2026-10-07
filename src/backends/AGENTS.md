<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# src/backends — local rules

`backends` provides platform implementations of the abstract interfaces declared by `src/platform/`
(`docs/spec/02-modules-and-interfaces.md` §10). Currently houses `backends/wayland`; macOS backends join in Phase 30.

- **Thread affinity rules.** Each class header states its thread affinity (`// Thread: GUI`, `// Thread: WaylandSession`,
  `// Thread: any`). `WaylandSession` opens its own `wl_display` and event queue, and dispatches only on its own dedicated
  worker thread (`wl_display_prepare_read_queue` → `poll` → `wl_display_read_events` → `wl_display_dispatch_queue_pending`).
  Never dispatch or call `wl_display_dispatch` on Qt's display (`docs/spec/04-capture-and-overlay.md` §3 rule 1).
- **Two Wayland connections with fixed ownership.** Qt's connection carries layer surfaces, text input, cursor shape,
  and pointer warp. AriadShot's own `WaylandSession` carries capture, data-control clipboard, virtual pointer, toplevel
  export, and the Hyprland global shortcuts fallback (`docs/spec/04-capture-and-overlay.md` §3).
- **No Qt GUI classes on the session thread.** Results reach the GUI thread only through queued signals with value types
  (`QImage` ownership is transferred, never shared mutably) (`docs/spec/04-capture-and-overlay.md` §3 rule 2).
- **Output and seat matching.** Outputs and seats are bound separately on each connection and matched by `wl_output.name`
  (v4+) to Qt's `QScreen::name()`. An output that cannot be matched is not captured, and the failure is logged at warning
  level (`docs/spec/04-capture-and-overlay.md` §3 rule 3).
- **Module boundaries and allowed edges.** `backends/wayland` may link `platform`, `wayland-client`, and `Qt6::Core`
  (`docs/spec/02-modules-and-interfaces.md` §1). It may NOT link `render` or `media`. The configure step and
  `scripts/check-architecture.sh` enforce this.
- **Platform capabilities.** Ask the capability registry, never the desktop name.
