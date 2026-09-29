<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 04. Capture and Overlay

Part of the [AriadShot technical specification](README.md). Decisions and their rationale:
[ADR 0001](../adr/0001-architecture-and-stack.md), [ADR 0002](../adr/0002-foundation-and-workflow.md).

## 1. Scope

This file specifies how AriadShot freezes the screen, shows the capture overlay, lets the user select and snap a
region, and routes the result. It covers the capture backends, the two Wayland connections, the capture ordering
contract, the surface hosts, the overlay state machine, selection and snapping, keys, the toolbars' placement, the
finish flows, the image editor, the overlay-class panels (thumbnail, history panel, pin, toast, countdown) and scroll
capture.

It does not repeat what other files own:

- pixels, canonical layers and the overlay's draw order: [03](03-rendering-contracts.md);
- the annotation model, undo entries and text editing: [06](06-annotation-model-and-formats.md);
- capability resolution and the per-desktop capability matrix: [08 §1–§2](08-platform-integration.md);
- global hotkeys: [08 §3](08-platform-integration.md); held and dropped capture requests: [08 §4](08-platform-integration.md);
- OCR, QR, translation and redaction engines: [09](09-ml-services.md); saving, history and filenames:
  [07](07-storage-history-settings.md).

Value tables that MacShot holds in machine-extractable form (the overlay key registry, the single-key table, tool
option specs, the modifier table) are generated into the parity ledger ([12](12-testing-strategy.md)); this file names
their source and the rules around them.

## 2. Capture backends per platform

All capture goes through `platform::CaptureBackend` ([02 §6](02-modules-and-interfaces.md)). The backend is chosen at
start-up from probed protocol globals and portal versions, never from the desktop name, and the result is recorded in
the capability registry.

| Platform | Stills | Window image with real alpha (beautify) | Notes |
| :--- | :--- | :--- | :--- |
| Hyprland and wlroots | `ext-image-copy-capture-v1` with one `ext_output_image_capture_source_manager_v1` source per output; `zwlr_screencopy_v1` as the per-output fallback | Hyprland: `hyprland_toplevel_export_v1`, addressed by the window address that Hyprland IPC returns with the window list. Other compositors: the `ext-foreign-toplevel` image capture source where advertised | Both globals are advertised on the reference host but neither is exercised yet [GATE G2]. Neither prompts the user when the compositor does not enforce permissions |
| KDE Plasma 6 | Probe `ext-image-copy-capture-v1` at run time (KWin may restrict it to privileged clients); otherwise a ScreenCast portal session with `persist_mode = 2` and a restore token | not available | The restore token is rotated after every successful `Start`. A token can be ignored by the portal (source gone, permission withdrawn), so a renewed picker is a normal state, not an error (R9) |
| GNOME (Mutter) | Screenshot portal with `interactive = false`, which returns one whole-desktop image; AriadShot splits it by output geometry | not available | `interactive = false` is only a hint: the backend may still show a dialog. When it does, the flow becomes "portal dialog, then AriadShot's overlay on the returned image" (DEV-26). No latency or one-time-consent promise is made; the behaviour is measured in a GNOME 48+ VM ([14 §3](14-gates-and-milestones.md#3-platform-feature-gates)) |
| X11 | XShm per screen | XComposite (named window pixmap with an ARGB visual) | |
| macOS | ScreenCaptureKit, displays captured **sequentially** with the pointer display first (the macOS 26 rect path, `macshot/Capture/ScreenCaptureManager.swift:223-299`); concurrent capture is not used because the system daemon serialises it | `SCContentFilter(desktopIndependentWindow:)` (`macshot/Capture/ScreenCaptureManager.swift:510-557`) | MacShot's fastest path; the macOS minimum is 14 (DEV-27) |

Window capture is needed only for window-snap beautify (CR-02, SV-01); M1 captures outputs only and window capture
arrives with beautify in M2 ([14](14-gates-and-milestones.md)).

## 3. Wayland connection ownership

AriadShot uses two Wayland connections with fixed ownership ([ADR 0001](../adr/0001-architecture-and-stack.md)).
Binding extra protocols on Qt's connection would make AriadShot's objects share Qt's event reading and queue dispatch,
and Qt's native interface carries a weaker compatibility promise.

| Connection | Owner and thread | Carries |
| :--- | :--- | :--- |
| Qt's connection | Qt, GUI thread | layer surfaces through `layer-shell-qt` (or the surfaces of the chosen host), text input, `wp_cursor_shape_v1`, `wp_pointer_warp_v1` (it needs a focused surface and a pointer serial) |
| `WaylandSession` | `backends/wayland`, its own thread | `ext-image-copy-capture-v1`, `zwlr_screencopy_v1`, `ext_data_control_v1`, `hyprland_toplevel_export_v1`, `zwlr_virtual_pointer_v1`, and `hyprland_global_shortcuts_v1` as the hotkey fallback |

Rules for `WaylandSession`:

1. It opens its own `wl_display` and its own event queue, and dispatches only on its own thread with
   `wl_display_prepare_read_queue` → `poll` → `wl_display_read_events` → `wl_display_dispatch_queue_pending`. It never
   calls `wl_display_dispatch` on Qt's display.
2. It uses no Qt GUI class. Results reach the GUI thread through queued signals carrying value types (`QImage`
   ownership is transferred, never shared mutably).
3. Outputs and seats are bound separately on each connection and matched by `wl_output.name` (version 4+) to Qt's
   `QScreen::name()`. An output that cannot be matched is not captured, and the failure is logged at warning level.
4. Output hot-plug on either connection triggers the display-change path: overlay teardown and pool rebuild
   (§5.4), capability re-resolution ([08 §1](08-platform-integration.md)).

Gate G4 proves that the two connections coexist: 10 minutes of repeated captures plus unfocused clipboard copies
with no stall longer than one frame, and a data-control copy that survives overlay dismissal. If G4 fails, the
protocols move to Qt's connection behind a Qt-version-pinned adapter ([14 §2](14-gates-and-milestones.md#2-m0-gates)).

## 4. Capture ordering contract

Advertised globals are not usable features. The sequence below is what gate G2 demonstrates, first on a synthetic
scene in a nested headless Hyprland and then in an owner-approved live test. It is written for image-copy; the other
backends follow the same order with their own primitives (§4.3).

### 4.1 The eight steps (Hyprland and wlroots)

1. **Hide own surfaces first.** Visible AriadShot surfaces (thumbnails, pins, toasts, the history panel, editor and
   Settings windows) are hidden under the deferred-restoration generation guard (SH-32, §7.5). If any was visible,
   capture waits until the compositor has presented a frame without it: `wp_presentation` feedback from the dormant
   overlay when it is mapped, otherwise a bounded delay of one refresh period of the output's current mode. The
   overlay itself never contributes pixels to the frozen frame: it is unmapped, or fully transparent with an empty
   input region, until its own output's frame has arrived.
2. **Select the source per output.** A source is created for each output with
   `ext_output_image_capture_source_manager_v1.create_source(wl_output)`, the pointer's output first. The `wl_output`
   belongs to `WaylandSession`'s connection and is matched to the `QScreen` by name (§3).
3. **Cursor semantics.** Stills omit the cursor unless `captureCursor` is on (MacShot default: off). When it is on,
   the session is created with the `paint_cursors` option bit, which matches MacShot's composite of the cursor on
   the pointer display only (`macshot/Capture/ScreenCaptureManager.swift:342-376`). The separate pointer-cursor
   session serves recording telemetry only ([05 §3](05-recording-and-studio.md)).
4. **Negotiate buffers.** The session announces `buffer_size`, the `shm_format`s and the DMA-BUF device and formats,
   then `done`. Stills use `wl_shm` in ARGB8888 or XRGB8888, because the pixels are needed on the CPU. With
   XRGB8888 the alpha channel is forced opaque when the image is wrapped.
5. **Receive the frame.** `transform`, `damage` and `presentation_time` arrive, then `ready`. The buffer is rotated
   upright by the `transform` (needed for rotated outputs), wrapped as a `QImage` with `QColorSpace::SRgb`, and its
   pixel-to-point scale is taken from the output's logical size (`buffer width / logical width`), never from an
   assumed integer scale. The screencopy fallback has no transform event: the transform comes from the output's
   `wl_output.geometry`, and the `flags` event's y-invert is honoured.
6. **Recover from failure.** On `failed`: `buffer_constraints` renegotiates once; `stopped` or `unknown` falls back to
   `zwlr_screencopy_v1` for that output; if the fallback fails too, the overlay on that output shows the error pill
   (OV-27) "Couldn't capture <output>", and the other outputs still open.
7. **Repeated captures.** A later frame of the same session may wait indefinitely for new damage, so stills create a
   fresh frame request per capture and never wait for damage. Whether a session is reused across captures or
   recreated is decided by G2's latency measurements [GATE G2].
8. **Map the overlay.** The overlay on each output maps (or turns opaque, in the dormant-mapped variant) only after
   that output's frame has arrived: the pointer's output first, the others progressively (§4.2).

### 4.2 Progressive multi-display capture and stitching

- **Pointer display first** (SH-16). The overlay on the pointer's output becomes interactive as soon as its frame is
  installed; the user can start a selection while the other outputs are still being captured. Later outputs never
  steal the keyboard: focus stays on the overlay that holds a selection, otherwise on the one under the pointer
  (`macshot/AppDelegate.swift:1440-1530`, `refocusOverlay`). The GNOME Screenshot portal returns one whole-desktop
  image, so GNOME cannot be pointer-display-first; its outputs appear together.
- **Stale callbacks.** Every capture carries the session ID (§7.1); a frame that arrives for an older session is
  discarded.
- **Cross-display stitching** (OV-23). A selection spanning outputs is composited from per-output native buffers.
  MacShot re-rasterises every display at the primary display's scale and truncates the output size
  (`macshot/AppDelegate.swift:2564-2621`); AriadShot fixes this (DEV-34) with the two-branch rule: if all involved
  outputs share one device pixel ratio, paint each buffer unscaled at `logicalPosition × dpr` into an image of
  `logicalSize × dpr`; otherwise allocate at `ceil(maxDpr)`, fill transparent, and scale each buffer to
  `logicalSize × ceil(maxDpr)` with nearest-neighbour for integer-DPR outputs and bilinear for fractional ones. The
  device pixel ratio is set on the result after painting. Global coordinates are signed (outputs left of or above
  the origin have negative positions).
- **Remote selection** (OV-22). All outputs share one global coordinate space. A selection rectangle is converted to
  each other output's local space and clipped to it; those outputs draw the remote border and handles and accept
  resizes, which are converted back to global coordinates and applied to the primary overlay
  (`macshot/AppDelegate.swift:3329-3383`).

### 4.3 Other backends and shared rules

- **macOS**: ScreenCaptureKit per display, sequential, pointer display first; if any display fails, the remaining
  ones use the fallback path and are installed as they arrive, without re-marking interactivity.
- **Portal (KDE fallback, GNOME)**: the portal image or stream frame replaces steps 2–5; step 1 still applies, and
  step 8 maps each output's overlay once the image is split.
- **X11**: XShm per screen after step 1; override-redirect overlays map afterwards.
- **Freeze before focus change** (CR-01). No AriadShot surface takes keyboard focus before the frames are captured,
  so other applications' menus, tooltips and popups are frozen as they were. On macOS the immediate path enumerates
  fresh shareable content instead of the cached list for the same reason (`macshot/Capture/ScreenCaptureManager.swift:136-220`).
- **Preview downscale** (CR-03). MacShot defines a display-preview helper that downscales to at most 1400 px on the
  longest side (`macshot/Capture/ScreenCaptureManager.swift:315-340`), but at the pinned commit only its tests call it.
  AriadShot uses the same rule only as the presentation-only mitigation of [03 §2](03-rendering-contracts.md) when G1
  requires it; the canonical source for export is always the full-resolution capture.
- **Budgets**: hotkey to interactive pointer output ≤ 100 ms p50 and ≤ 150 ms p95; all outputs ≤ 250 ms p95 (W1,
  [HYPOTHESIS] until G8, [12](12-testing-strategy.md)).

## 5. Surface hosts

### 5.1 Host contract

A surface host implements `platform::SurfaceHost` and presents a `platform::SurfaceContent` (a `ui::ViewRoot`)
([02 §6](02-modules-and-interfaces.md)). Every host MUST:

1. create one surface per role and output (`Overlay` on every output; the other roles on the output they are placed
   on) and destroy it only on teardown (§5.4);
2. present the content's layers (canonical layers and chrome, in draw order) with damage rectangles only, and never
   re-render annotations ([03 §2](03-rendering-contracts.md)); at 1:1 on an integer-scale output presentation is a blit;
3. forward pointer, wheel, tablet (pressure for the pencil, AN-05), keyboard and input-method events to the content,
   and report enter/leave for hover;
4. set cursor shapes through `wp_cursor_shape_v1` (or the platform equivalent), including the hidden cursor under the
   pencil dot (`macshot/UI/Overlay/OverlayView.swift:1312`, `:1503`);
5. grant exclusive keyboard to exactly one overlay surface at a time: the one holding the selection, otherwise the one
   on the pointer's output. Moving the pointer to another output while no selection exists hands the keyboard over
   without losing keys, and Esc works from either output. Panels use on-demand keyboard (the pin needs Esc; the
   thumbnail and toast need none);
6. anchor overlay surfaces on the OVERLAY layer to all edges with exclusive zone −1, so they cover panels and bars
   (OV-01).

Popovers, tooltips and right-click menus opened inside an overlay are chrome view objects drawn inside the same
surface, so no `xdg_popup` from a layer surface is needed. Menus opened from ordinary windows may be Qt menus.

### 5.2 Hosts

| Host | Surfaces | Chosen when |
| :--- | :--- | :--- |
| H1 `LayerShellWidgetHost` | `QWidget` top-levels on `layer-shell-qt`, configured before the window is shown | default on layer-shell compositors |
| H2 `LayerShellQuickHost` | `QQuickWindow` on `layer-shell-qt`, layers uploaded as textures with damage, no QML | G1 selects it (outcomes O2–O6) |
| H3 `WaylandLayerHost` | own layer-shell client on its own connection: `wl_shm`, `wp_viewporter`, fractional scale, xkbcommon, `text-input-v3` | both Qt hosts fail G1 step 1 or 2 (O13); loses Qt accessibility on overlays |
| `PanelHost` | non-activating `NSPanel` at level 257 (overlay) and 258 (panels above it), on all Spaces, full-screen auxiliary | macOS |
| `OverrideRedirectHost` | override-redirect fullscreen windows; above and sticky hints for panels | X11 |
| `FullscreenToplevelHost` | a frameless fullscreen `xdg_toplevel` per output via `set_fullscreen(output)`, activated with the shortcut's activation token | GNOME and any compositor without layer-shell; panel coverage, on-top and exclusive grabs are not guaranteed (DEV-26) |

### 5.3 Gate G1 in brief

G1 runs H1 and H2 through seven steps on topology P1 and on a nested Hyprland at fractional scale 1.25: (1) mapping
per output including a rotated one, (2) keyboard hand-over, (3) chrome popovers and the Save menu, (4) IME in the
canvas text control (Vietnamese Telex and a CJK engine), (5) re-show latency ≤ 150 ms p95 in the dormant-unmapped and
dormant-mapped variants, (6) drag frame time ≤ 16.7 ms p95 on P1 and P3, (7) cursor shapes. The decision rule and the
fourteen outcomes are in [14 §2](14-gates-and-milestones.md#2-m0-gates). The IME fallback (DEV-21) is specified in
[06 §3](06-annotation-model-and-formats.md).

### 5.4 Overlay pool, dormancy, dismiss and teardown

- **Pool** (SH-15). One overlay is created per output at start-up and kept alive between captures; it is rebuilt when
  the display topology changes (`macshot/AppDelegate.swift:444-470`).
- **Dormant variants** [GATE G1, G8]. *Dormant-unmapped*: the surface object exists but is unmapped; a capture maps
  it. *Dormant-mapped*: the surface stays mapped, fully transparent, with an empty input region and no keyboard; a
  capture needs one commit. G1 step 5 measures both; the dormant-mapped variant is adopted only if G1 and G8 show it
  does not disturb blur rules or power use.
- **Dismiss** (OV-29). Ends a session: saves the last selection when it is larger than 1×1 pt, resets the view,
  releases the frozen image and every canonical layer, returns the surface to its dormant state and restores the
  cursor. The surface, host and view objects stay alive (`macshot/UI/Overlay/OverlayWindowController.swift:427-473`).
  The daemon holds no screenshot pixels afterwards.
- **Teardown** happens only on a display change or at quit: delegate unbound, content removed, surface destroyed.

## 6. Overlay state machine

`ui::OverlayCanvas` ports MacShot's `OverlayView` state machine (`macshot/UI/Overlay/OverlayView.swift:154-160`).

| State | Entered by | Behaviour |
| :--- | :--- | :--- |
| `idle` | overlay shown; click outside after clear | crosshair; snap highlight under the pointer; idle helper card (OV-04); Tab cycles snap mode; F, R and click rules below |
| `selecting` | left drag, or right-click anchored selection | rubber band from the anchor; selecting badge (OV-05); boundary snap of the moving corner; Space moves rigidly; Shift squares |
| `selected` | mouse-up, click on a snap target, F, R, applied selection | toolbars, options row, resolution box, handles; tools draw annotations |

Transitions that are easy to get wrong:

- **Click without drag in `idle`.** With a snap mode on, it selects the hovered window or element (and, for a window,
  requests a separate window image with real alpha for beautify). With snap mode off, it selects the full output.
- **F** selects the full output only when `snapMode != .off`; in quick-capture mode it quick-saves immediately
  instead of entering `selected` (`macshot/UI/Overlay/OverlayView.swift:9179-9194`).
- **R** restores the last selection only in `idle`, outside the editor, while no text is being edited; once a
  selection exists R is the rectangle tool's key (`:9196-9203`).
- **Outside clicks in `selected`.** A single click outside the selection is inert and never discards annotations; a
  double click outside confirms (OV-20, §8.6).

Flags configure specialised flows (`:720-775`): editor mode (`EditorCanvas`), scroll capturing, recording (setup bar),
auto OCR, auto quick-save, auto record, auto translate overlay, auto scroll capture, and auto confirm (Add Capture in
the editor). The recording popover's FPS, "When done", delay and "Hide controls" values are session overrides held on
the canvas; they never write settings (`macshot/UI/Overlay/OverlayView+Popovers.swift:240-378`).

The canvas reports every outcome through `ui::OverlayDelegate`, MacShot's 29-callback output API ported one to one
(`:6-41`, [02 §7](02-modules-and-interfaces.md)). `app::AppController` implements it for capture overlays and
`windows::EditorWindow` for the editor.

## 7. Capture sessions

### 7.1 Capture gate (SH-13)

`AppController` starts a capture only when no capture is running and nothing is recording
(`macshot/AppDelegate.swift:1242-1302`). Starting a capture:

1. sets the capturing flag and increments the session ID (wrapping); callbacks from older sessions are discarded;
2. remembers the previously focused window and resolves its title asynchronously through `WindowDirectory` (used by
   the filename `{window}` token and focus return; unavailable where the capability registry says so);
3. when `rememberLastTool` is false, resets the remembered tool, clears the stored effects keys and turns beautify
   off;
4. dismisses stale overlays without returning focus, hides own surfaces under the generation guard (§7.5) and hides
   thumbnails;
5. starts the countdown when `captureDelaySeconds > 0` (§7.2), otherwise captures (§4).

Requests that arrive while capture is not yet possible (capability probing, portal consent) are held or dropped by the
rules of [08 §4](08-platform-integration.md).

### 7.2 Countdown (SH-14)

A 140×140 pt borderless surface centred on the preferred output (centred on the selection and click-through for the
pre-recording countdown), drawing a 120 pt circle in black 0.7, a 3 pt ring in white 0.6 inset 3 pt, and the remaining
seconds in a 52 pt bold monospaced-digit font (`macshot/UI/Windows/CountdownView.swift:9-33`,
`macshot/AppDelegate.swift:1304-1382`). The digit decrements once per second. Esc cancels through a key handler on the
countdown surface (MacShot installs a local monitor only), removes the surface and clears all pending mode flags
(record, full screen, full-screen record and auto-start, OCR, translate overlay and its language, quick capture,
scroll capture, restore last area). When no output is available the capture gate is released instead of staying stuck
(DEV-11, a fixed upstream defect).

### 7.3 Specialised modes

| Mode | Entry | Behaviour |
| :--- | :--- | :--- |
| Quick Capture (SH-18) | hotkey slot, status menu, `quick-capture` action | toolbars hidden; mouse-up runs the Quick Save route (§12.2) |
| Quick OCR (SH-19) | slot, menu, `ocr` action | mouse-up runs text and QR recognition; `ocrAction` 0 = window + copy (default), 1 = window, 2 = copy only and return focus |
| OCR-translate (SH-20) | `ocr-translate?target=<lang>` | recognise, translate, and draw translated text in place as translate-overlay annotations (AN-15) |
| Repeat last area (SH-21) | slot, menu, `capture-last` | the saved rectangle applies only to an output whose logical frame is identical to the saved one; keyboard focus follows that overlay (`macshot/AppDelegate.swift:1622-1638`) |
| Full screen | slot, menu, `capture-fullscreen` | the pointer's output is selected on install |
| Scroll capture | slot, menu, `scroll-capture` | §15 |
| Record | slot, menu, `record`, `record-fullscreen` | recording setup bar; [05](05-recording-and-studio.md) |

### 7.4 Focus return

After a session ends, focus returns to the previously focused window where the platform allows it (Hyprland IPC
`focuswindow`, EWMH activate on X11, `NSRunningApplication` on macOS); elsewhere the compositor decides
([08 §2](08-platform-integration.md)).

### 7.5 Deferred restoration (SH-32)

Own windows hidden for a capture are tracked by a generation-guarded list ported from
`macshot/Services/DeferredRestoration.swift:1-34`: `begin(adding:)` bumps the generation and merges items,
`schedule()` returns a token, `take(ifCurrent:)` restores only when the token is still current, and removing the last
pending item bumps the generation. A new capture that starts before the previous restoration fires inherits the still
hidden windows and invalidates the old callback, so stale windows never flash into the next capture.

## 8. Selection

### 8.1 Chrome geometry (OV-15)

- Dim black 0.45 over the whole output outside the selection unless `disableSelectionOutsideShadow`
  (`macshot/UI/Overlay/OverlayView.swift:1736-1745`); the selection shows the frozen image undimmed.
- Border 2 pt in the theme accent; 2.5 pt system red during scroll capture; not drawn in the editor or while the
  beautify or effects preview is shown (`:2119-2126`).
- Eight handles of 10 pt; corner hit areas expanded by 2 pt to 14 pt and tested before edges; edge zones 6 pt thick
  and excluding the 14 pt corner squares (`:1098-1106`, `:5493-5537`).
- The cursor waterfall, from popover and chrome down to the tool cursor, is MacShot's `updateCursorForPoint`
  (`:1358-1516`); pencil and marker use an invisible cursor because the drawn dot is the cursor.

### 8.2 Modifiers and movement (OV-16)

Shift constrains to 1:1; Option resizes from the centre; a locked aspect ratio derives the dependent dimension on
corner and edge drags (`:7189-7521`). Holding Space moves the rectangle rigidly during `selecting` or `selected`.
Arrow keys nudge by 1 pt, Shift+arrow by 10 pt. Right-click in `idle` starts an anchored selection that follows the
pointer without a held button; the next click commits and Esc cancels it. The per-handler modifier rules are ledger
lines generated from MacShot's handlers.

### 8.3 Resolution box and presets (OV-17, OV-18, OV-06)

- **Box.** Width `6 + 56 + 4 + max(12, glyph) + 4 + 56 + 4 + 30 + 6` pt, height 34 pt; W and H fields 56×22 pt
  accepting integers 1…100,000; a 30 pt presets button, accent-tinted when a preset is active
  (`macshot/UI/Overlay/ResolutionBoxView.swift:24-34`, `:96-104`). The "×" glyph is centred on the selection's midX.
- **Placement** (`OverlayView.swift:2431-2496`): candidates above and below the selection with an 8 pt gap
  (`handleSize / 2 + 3`), then inside-top and inside-bottom, then the least-overlap candidate; obstacles are the
  bottom bar, options row and right bar inflated by 4 pt, plus the output's top obstruction; x is clamped 2 pt inside
  the output. On macOS the obstruction is the notch; on Linux it is the output's reserved zone where the platform
  reports one (Hyprland IPC `reserved`), otherwise none.
- **Units.** `resolutionUnitIsPoints` toggles pt and px; px multiplies by the output's buffer scale. The ratio label
  prints `-` for a non-positive aspect, `N : 1` for an integer aspect, a GCD-reduced `W : H` when a denominator ≤ 32
  matches within 0.01 and both terms are ≤ 32, else `%.2f : 1` (`:2646-2657`).
- **Typed sizes** (`:3006-3104`). With a locked aspect and one edited field, the partner is
  `max(1, round(w / aspect))` or `max(1, round(h × aspect))`; the new rectangle is centred on the old centre and
  clamped to the output, preserving aspect when it would not fit.
- **Presets** (`macshot/UI/Overlay/ResolutionPresets.swift:28-53`): freeform, nine ratios (1:1, 4:3, 3:2, 16:10, 16:9,
  21:9, 5:1, 3:4, 9:16), a live "Custom · W:H" row when the current aspect matches none, seven fixed sizes (1920×1080,
  1920×384, 1280×720, 1080×1080, 1080×1920, 800×600, 640×480), "Keep ratio for next captures" (`keepAspectRatio`),
  the unit switch, and Auto Adjust.
- **Pre-selection preset button** (OV-06). A 34×28 pt button inside the idle helper card, shown only in `idle`
  outside the editor, recording and auto OCR, with no remote selection (`OverlayView.swift:2343-2366`). A chosen exact
  resolution resets to freeform when the user resizes away from it; a kept ratio is applied to the next fresh
  selection.

### 8.4 Auto Adjust (OV-11)

Each edge of an existing selection (≥ 4×4 pt, not in the editor) snaps to the nearest qualifying boundary within
`min(160, max(48, 0.30 × dimension))` pt, scored only across the central span (15 % inset at both ends); the result
must stay at least 4 pt wide and tall (`OverlayView.swift:7353-7449`). If the boundary index is still building, the
overlay shows the error-pill text "Detecting nearby edges…" and applies the adjustment when the index is ready.

### 8.5 Radial colour wheel (OV-19)

Right-click inside a `selected` selection opens a wheel of 16 swatches (12 hues and 4 neutrals) on a ring of radius
72 pt, swatch radius 12 pt (15 pt hovered), with a dead zone of `0.25 × 72 = 18` pt at the centre
(`macshot/UI/Overlay/ColorWheelRenderer.swift:12-13`, `:128`). Drag-and-release picks; a release without a drag
leaves it open (sticky) for a click pick; Esc dismisses a sticky wheel. Right-click on a line or arrow adds a waypoint
instead (AN-03).

### 8.6 Double click (OV-20)

With `doubleClickToCopy` on (default true): a double click inside the selection rewinds the annotations created by
the first click to the recorded undo baseline and then confirms; a double click outside confirms directly; a double
click on a text annotation edits the text and never copies (`OverlayView.swift:6653-6727`).

## 9. Snapping, measuring and sampling

### 9.1 Snap modes and the Tab cycle (OV-07, DEV-15)

MacShot cycles window → off → element → window, persists `captureSnapMode` (0 window, 1 element, 2 off) and tells the
other outputs' overlays to redraw (`macshot/UI/Overlay/OverlayView+WindowSnapping.swift:16-28`,
`OverlayView.swift:9287-9305`). Entering element mode without the permission keeps element mode selected, shows
"Accessibility Access Required" and requests the permission.

AriadShot keeps the order but skips modes the capability registry reports unavailable. On Hyprland, Sway and X11
(no element snapping) the cycle is window → off → window; where no window list exists (KDE, GNOME Wayland) only "off"
remains and Tab does nothing. The idle card's first line uses MacShot's text for the effective mode
(`OverlayView.swift:2304-2356`); lines for unavailable modes never appear; with a single available mode the second
line is "Snap mode: OFF" without "(Tab to switch)". A stored mode that names an unavailable mode is coerced at load to
the next available mode in the order element → window → off, without rewriting the stored value. Click-without-drag
in "off" still selects the full output.

### 9.2 Window snapping (OV-08)

`WindowDirectory::windowsFrontToBack` returns candidate windows for the output. MacShot's filter is kept: normal-layer
windows only, plus the Quick Look exception; both dimensions greater than 10 pt; the first window containing the
pointer wins, except the Finder preview heuristic (a same-owner Finder candidate that is untitled, smaller than 0.85 of
the front window's area and at least 80×80 pt) (`OverlayView+WindowSnapping.swift:46-165`). The highlight is system
blue: 0.08 fill, 2 pt border at 0.85, radius 4 (`:444-450`). The query is asynchronous under the platform completion
contract ([02 §6](02-modules-and-interfaces.md)): it runs on the backend's thread, carries the capture session ID, and
queries coalesce while one is in flight.

Per platform: Hyprland IPC (`clients` with geometry, workspace and focus history; front-to-back order resolved from
fullscreen, floating and focus-history state), Sway IPC, EWMH `_NET_CLIENT_LIST_STACKING` on X11, `CGWindowList` on
macOS; unavailable on KDE and GNOME Wayland (DEV-17), where boundary snapping remains.

### 9.3 Element snapping (OV-09)

macOS only: accessibility-tree traversal with depth 8, at most 48 visited nodes, 64 children per node and a 35 ms
deadline, the Chromium enhancement, and retries after 0.1, 0.5 and 2.1 s (`OverlayView+WindowSnapping.swift:170-332`).
Unavailable on Linux (DEV-16).

### 9.4 Boundary snapping (OV-10)

The index is built once per frozen image on a worker in the audited pixel module (`render/pixel/BoundarySnapIndex`):
per-pixel RGB Euclidean differences between neighbouring columns and rows, skipped above 40 MP
(`macshot/Services/BoundarySnapIndex.swift:40-53`). A query scans boundaries within `round(4 pt × scale)` of the
dragged edge, over the perpendicular span of the selection only; a boundary qualifies when its mean difference ≥ 28
and its support fraction (samples ≥ 28) ≥ 0.55; the nearest qualifying boundary wins. Snapping of the moving corner
is suppressed by Option, an active pre-selection ratio, Shift and Space repositioning (`OverlayView.swift:6858-6869`).
Guides are accent 0.9, 1 pt, spanning the whole output.

### 9.5 Alignment snapping (OV-12)

Annotation moves and selection edits snap within 5 pt to the selection's and other annotations' min, mid and max
coordinates (`OverlayView.swift:653`, `:3925-4047`); guides are system cyan 0.6, 0.5 pt, dashed [4, 3], spanning the
selection only.

### 9.6 Auto-measure (OV-13) and colour sampler (OV-14)

- Holding 1 (vertical) or 2 (horizontal) with the measure tool scans outward from the pixel under the pointer until
  the L1 RGB difference from the base pixel is ≥ 30; rays are clamped to the selection when the pointer is inside it
  and `measureClampToSelection` is on (default true) (`OverlayView.swift:4108-4220`, `:580`, `:9323-9336`).
- The sampler reads one sRGB pixel, returns nothing for alpha 0, un-premultiplies with rounding and formats
  `#RRGGBB` (`:3548-3595`); its pill is black 0.85, radius 6, a 12 pt monospaced hex, a 16 pt swatch and "Right-click
  to copy", offset 16 pt right and 8 pt below the pointer. Sampled colours fill the custom colour slots (AN-20, DEV-09).

### 9.7 Error pill (OV-27)

Red (0.8, 0.2, 0.2, 0.9), radius 8, 13 pt medium white text, width text + 24 pt, height text + 12 pt, top-centred
40 pt below the output's top edge, hidden after 4 s (`OverlayView.swift:2274-2293`, `:5109-5118`). Every capture,
download and action failure on an overlay uses it.

## 10. Keys

- **Overlay key registry** (OV-21): Tab, F, R, the seven-level Esc waterfall (cancel scroll capture → cancel anchored
  selection → dismiss sticky wheel → cancel text editing → dismiss popover → deselect annotations → cancel the
  overlay), Return and keypad Enter (Quick Save route), Delete and forward-delete, copy, paste and duplicate with a
  (+15, −15) pt offset, undo and redo, save and save-as, and hold 1/2. The registry is MacShot's `performKeyEquivalent` and `keyDown` handlers
  (`OverlayView.swift:9086-9340`) and is generated into the ledger. On Linux ⌘ becomes Ctrl.
- **Single-key shortcuts** (SH-11): 27 customisable keys for tools and actions, empty meaning disabled
  (`macshot/Services/ToolShortcutManager.swift`), generated into the ledger.
- **Layout-aware matching** (SV-08): keys match the character the active layout produces; for non-Latin layouts the
  character from the most recent ASCII-capable layout is the fallback; a key with no character never matches a
  character binding (`macshot/Services/KeyboardShortcutMatcher.swift`). On Linux the character comes from the
  xkbcommon keysym of the event, never from the raw keycode.
- Keys are ignored while the canvas text control has focus, except those the text control itself handles
  ([06 §3](06-annotation-model-and-formats.md)).

## 11. Toolbars, options row and chrome placement

### 11.1 Chrome views (TB-01 … TB-15)

Toolbars are chrome view objects drawn inside the overlay or the editor's view root, with solid fills and no blur
(TB-01). Theme defaults: accent `#8C4DD9`, icons white, background `#1F1F1F`; controls use the light appearance when
the background's brightness exceeds 0.5 (`macshot/UI/Toolbar/ToolbarDefinitions.swift:239-306`). Theme presets
(TB-02), strip and button geometry and states (TB-03, TB-04: 32×32 buttons, radius 6, padding 4, spacing 2; pressed
accent 0.6, on accent, hover icon 0.12; click only on mouse-up inside; gap clicks swallowed in the overlay and passed
through in the editor), icons (TB-05), tooltips (TB-06: instant, shortcut suffix only with
`showToolShortcutsInTooltips`), button inventories and order (TB-07 bottom bar, TB-08 right bar, TB-09 recording
setup bar; `ToolbarDefinitions.swift:309-525`), customisation with migration guards (TB-10), the per-tool options rows
(TB-12), popovers (TB-14) and right-click menus (TB-15) are ported one to one; their values are ledger lines. Share is
hidden on Linux (DEV-18); upload is absent in the offline build ([10 §6](10-upload-and-network.md)); remove background
is hidden where no engine or model exists ([08 §1](08-platform-integration.md)).

### 11.2 Placement (TB-11, TB-13)

`OverlayView.repositionToolbars` (`OverlayView.swift:5216-5460`) is ported as one function:

1. The anchor is the selection, or the beautify-expanded rectangle (padding, plus a 28 pt title bar in window mode),
   animated at 60 Hz by +0.08 per tick with ease-out `1 − (1 − t)²` when beautify toggles (OV-25).
2. The right bar goes right of the anchor if it fits with a 50 pt margin, else left, else inside the right edge; a
   narrow selection that fits neither side puts it below, right-aligned; it aligns with the anchor's top and is
   clamped 4 pt inside the output.
3. The bottom bar goes below with a 6 pt gap if it fits with the 38 pt options-row reservation, else above, else
   inside; overlap with the right bar is resolved by moving the right bar (right, left, down, up), keeping the bottom
   bar centred.
4. The right bar then avoids the resolution box (inflated by 6 pt) and the top obstruction.
5. The options row (34 pt tall, padding 8, radius 6, minimum width 200, width `max(bottom bar, content)`) sits 2 pt
   below the bottom bar in the overlay and 2 pt above it in the editor, clamped 4 pt inside.

In the editor, the bottom bar is centred 20 pt above the canvas bottom and the right bar pinned 20 pt from the right
and 36 pt from the top. Options-row edits to a selected annotation take one property snapshot on the first change and
commit one undo entry on deselect or tool switch ([06](06-annotation-model-and-formats.md)).

## 12. Finish flows and output routes

### 12.1 Confirm route (SH-17, OV-28)

Triggered by the Copy button, the copy chord and double click. It renders the selection with its annotations once,
snapshots the annotations, edit state and raw image for re-editing, plays the capture sound (`playCopySound`, default
true), dismisses the overlay, applies effects then beautify (for a window snap, annotations are composited onto the
separately captured window image, which receives the effects too), **always** copies the result to the clipboard, and
hands it to history and the thumbnail (`macshot/UI/Overlay/OverlayWindowController.swift:589-654`). It never consults
`quickCaptureMode`.

### 12.2 Quick Save route

Triggered by Return or keypad Enter in `selected` and by mouse-up in Quick Capture. After the same render, dismiss and
post-processing it follows `quickCaptureMode` (`macshot/Services/ImageSaveService.swift:31-50`): 0 save to file,
1 copy image (default when unset), 2 save and copy image, 3 do nothing (thumbnail and history only), 4 save and copy
the file path. With `quickCaptureOpenEditor` it then opens the editor.

### 12.3 Other outputs

| Output | Behaviour |
| :--- | :--- |
| Save | follows `saveAction`: 0 save to the configured folder (sound after the write), 1 ask where to save (`OverlayWindowController.swift:1061-1068`) |
| Save As | shows the file chooser above the overlay; on cancel the overlay stays, regains input and keyboard focus (`:1070-1093`) |
| Pin | beautify applied, sound, dismiss, pin panel (§14.3) |
| OCR | recognition on a worker, then sound, dismiss and the OCR window per `ocrAction` ([09](09-ml-services.md)) |
| Upload | online build only; beautify applied, sound, dismiss, upload toast ([10](10-upload-and-network.md)) |
| Open in Editor (detach) | hands the capture to a new `EditorWindow` (§13) |
| Share | macOS only: the picker opens with a 0.5 s re-entry throttle and the overlay lowered to a normal level while it is open; picking dismisses, closing the picker keeps the overlay (`:695-752`). Hidden on Linux (DEV-18) |
| Remove background | subject mask ([09](09-ml-services.md)); result copied and shown in a thumbnail |
| Record, scroll capture | [05](05-recording-and-studio.md), §15 |

Every output that leaves the application uses the canonical final image ([03 §2](03-rendering-contracts.md)).
Clipboard ownership rules (focused copy versus data-control) are in [08](08-platform-integration.md).

## 13. Image editor (ED-01 … ED-04)

`windows::EditorWindow` embeds an `EditorCanvas` view root in a scrolling, centring view with a top bar
(`macshot/UI/Editor/DetachedEditorWindowController.swift`, `EditorTopBarView.swift`, `CenteringClipView.swift`).

- **Window** (ED-01). Title "AriadShot Editor · HH:mm:ss" (brand per DEV-01); minimum 800×400; initial size
  `min(0.9 × screen, max(minimum, image + chrome))` with chrome allowances 106×156 pt; maximum the screen; content
  insets bottom 84 and right 50 with negated scroller insets; initial zoom fits the viewport but never exceeds 1, and a
  fitting image scrolls to its top (`DetachedEditorWindowController.swift:80-151`, `:211-236`). Zoom 0.1–8.
  Documents smaller than the viewport are centred, and clicks in the surrounding area inside the document bounds reach
  the canvas.
- **Top bar** (ED-02). 32 pt: size label in pixels, Crop, Flip Horizontal, Flip Vertical, Add Capture, Done (shown
  only while dirty), and a zoom menu (Zoom In ×1.25 on Ctrl+Plus, Zoom Out ÷1.25 on Ctrl+Minus, Fit on Ctrl+1, 50 %,
  100 % on Ctrl+0, 200 %).
- **Dirty state** (ED-03). Dirty when the capture was never output, the undo identity differs from the baseline, or
  the edit state differs. Closing a dirty editor asks "Save changes?" with Save & Close (default), Discard and Cancel.
  Ctrl+Q closes the editor window, not the daemon (SH-31, DEV-12).
- **Outputs** (ED-04). The overlay's outputs, with `closeEditorAfterCopy`, history auto-save, the thumbnail after
  copy, and Done committing the revision to history ([07](07-storage-history-settings.md)).
- **Extension points.** `EditorCanvas` overrides the 14 extension points of `OverlayCanvas`
  ([02 §7](02-modules-and-interfaces.md)): the document anchors the selection at the image bounds, the selection
  cannot be resized or replaced, the image is not clipped, no border or resolution box is drawn, detach is disabled,
  and top-chrome clicks go to the top bar.
- **Transforms** (OV-33). Flip mirrors the image and every annotation's points across the selection's mid-line; Add
  Capture runs a nested capture overlay with auto-confirm, adds the result as a capture stamp below the canvas and
  expands the canvas to the union of the opaque image content and all annotations; invert inverts the image and the
  window image; crop commits when the crop drag is larger than 4×4 pt, crops in pixel space and shifts annotations.
  Each is one image-transform undo entry holding the previous images and the annotation offsets
  (`macshot/UI/Overlay/OverlayView.swift:3599-3920`, `:4393-4462`). Paste of an image (Ctrl+V outside text editing)
  follows the Add Capture placement. Crop and rotate undo move annotations back with the image (DEV-08).

## 14. Overlay-class panels

Each panel is a `ui` view object shown through a surface host (§5). On GNOME they are normal windows without on-top,
placement or all-workspace guarantees (DEV-26).

### 14.1 Floating thumbnail (ED-05)

240×160 pt × `thumbnailScale`, radius 12, 1.5 pt white 0.4 border, placed in `thumbnailCorner` (default bottom
right) and stacked 8 pt apart upward from bottom corners and downward from top corners; hover shows a 0.45 veil,
four corner buttons (close, pin, edit, upload — upload absent offline) and Copy and Save pills; clicking the body
dismisses. Slide-in 0.3 s, reflow 0.25 s, dismiss 0.4 s; auto-dismiss after `thumbnailAutoDismiss` s (default 5,
0 = never), paused while hovered. Edgeward drag and swipe dismiss; a drag beyond 8 pt in another direction exports the
image as a file in the chosen format (`macshot/UI/Windows/FloatingThumbnailController.swift:240-1185`). On layer-shell
compositors it is an OVERLAY-layer surface anchored to its corner with margins and no keyboard; swipe dismiss works
only while the pointer is over it. Its right-click menu is the shared image context menu (ED-06), with Linux
substitutes for Share, Quick Look and Open With (DEV-18, [08](08-platform-integration.md)).

### 14.2 History panel (ED-07)

A backdrop surface that catches clicks and Esc, plus a 240 pt panel `min(output width − 40, 1200)` wide that slides
down under the top edge in 0.12 s (`macshot/UI/Windows/HistoryOverlayController.swift:21-60`); tabs All, Screenshots,
GIFs; a trash button with confirmation; 200×160 cards with captions and a hover hint; an LRU preview cache of 150 with
12 look-ahead; arrow, Return, Space (Quick Look substitute), Ctrl+E, Ctrl+S and Delete keys; drag to other
applications hides the panel during the drag. MacShot dismisses it when the app resigns active; AriadShot dismisses
it when its surface loses keyboard focus.

### 14.3 Pin (ED-08, ED-09)

Initial size ≤ 80 % of the output, aspect-locked, with a shadow and a 1 pt white 0.3 border at radius 6; zoom 0.1–5
anchored at the pointer (wheel sensitivity 0.03, precise 0.005, pinch); top-right close, edit and zoom-percentage pill,
where clicking the pill resets to 100 % around the current centre; a three-item menu (Copy to Clipboard, Save As…,
Close); Esc and Ctrl+Q close it; no opacity control (`macshot/UI/Windows/PinWindowController.swift:18-155`,
`:265-307`). On layer-shell compositors the user moves a pin by dragging it; AriadShot moves the surface by updating
its layer margins. Pin from clipboard renders images, and from M2 RTF, sanitised HTML and plain text cards
([09](09-ml-services.md), [11](11-security-privacy.md)).

### 14.4 Upload toast (ED-11)

380×56 pt, radius 14, top-centred 12 pt below the output's usable top edge; slide in 0.3 s, out 0.35 s; grows for
wrapped links and errors; dismissed after 8 s on success and 6 s on error, or on click
(`macshot/UI/Windows/UploadToastController.swift:15-253`).

### 14.5 Recording outline and HUD

The recording outline (OV-30), HUD, keystroke pill and webcam bubble are specified in
[05](05-recording-and-studio.md).

## 15. Scroll capture (CR-04 … CR-06, OV-35)

### 15.1 Session

The overlay switches to scroll capture on the selected region: the region becomes a transparent, input-transparent
hole, the border turns red 2.5 pt, toolbars hide, and the HUD appears (`OverlayView.swift:771-775`). The HUD and its
Stop button appear before the first settled frame; Stop or Cancel during that wait aborts cleanly without starting
scroll handling. Esc cancels. Frames are captured on demand for the region only; everything AriadShot draws during the
session (border, HUD, preview) MUST lie outside the captured region on backends that cannot exclude surfaces, which
G2's own-pixel test verifies [GATE G2].

### 15.2 Algorithm (identical on all platforms)

Ported from `macshot/Capture/ScrollCaptureController.swift` and `ScrollFrameAnalyzer.swift`; the pixel loops live in
`render/pixel/ScrollFrameAnalyzer`.

- **Settle.** Up to 30 attempts until two consecutive frames are byte-identical, waiting 10 ms and growing ×1.5 to a
  cap of 80 ms. The initial and manual captures fall back to the last frame after 30 attempts; an auto-scroll
  comparison that fails to settle counts as a miss (`ScrollCaptureController.swift:318-357`, `:449-487`).
- **Difference metric.** SAD `|ΔR| + |ΔG| + |ΔB|` with noise threshold 8, row offsets from the buffer's stride
  (`ScrollFrameAnalyzer.swift:80-87`).
- **Scrollbar margin.** Scan up to `min(50, width / 8)` columns inward from the right between 20 % and 80 % of the
  height; a detected width of 3–40 px gives a right margin of width + 4 px (`ScrollFrameAnalyzer.swift:94-123`,
  `ScrollCaptureController.swift:731-741`).
- **Sticky header.** The first changing row from the top (sampled every 4 px, excluding the margin); a candidate of
  10 px up to 60 % of the height locks on the first sample and must stay within ±5 px on later samples, else the
  header is cleared (`ScrollFrameAnalyzer.swift:130-151`, `ScrollCaptureController.swift:745-775`).
- **Registration.** Vertical shift between the previous and current frame with the header and right margin cropped.
  MacShot uses Vision translational registration; AriadShot uses phase correlation (pocketfft) with a SAD strip search
  as fallback ([09](09-ml-services.md)). A shift must be finite, within ±height, and at least height / 10; the merge
  uses `max(1, shift − 1)` px so the new frame overwrites the seam row.
- **Merge.** The canvas grows incrementally by the shift; with a sticky header only the new bottom slice is drawn.
- **Stop conditions.** Height ≥ `scrollMaxHeight` (default 30,000 px), 8 consecutive misses, 6 consecutive zero shifts
  after scrolling began, or the user.

### 15.3 Auto-scroll and manual mode (CR-05)

Auto-scroll warps the pointer to the region's centre and sends line-wheel bursts through `platform::InputSynth`:
speed 1 = 1 line × 1, 2 = 1 × 2, 3 (default) = 1 × 3, 4 = 2 × 4 per tick (`ScrollCaptureController.swift:372-391`).
It needs the virtual pointer and pointer warp (Hyprland, wlroots), XTest (X11) or CGEvent (macOS, with the
Accessibility permission); where none exists the control is hidden and only manual mode remains (DEV-25). Manual mode
captures every 150 ms while the user scrolls and a settled frame 250 ms after scrolling stops. MacShot detects
scrolling from global wheel events, which Wayland does not deliver to other clients; there the 150 ms cadence and the
250 ms settle rule are driven by frame changes instead (DEV-41) [GATE G2].

### 15.4 HUD and preview (CR-06)

The HUD shows the strip count, composite size, Auto Scroll / Stop and Cancel. The live preview panel is 200 pt wide,
placed 12 pt beside the region on the side with more room, bottom-anchored to the region and growing upward, clamped
below the output's top (`macshot/UI/Overlay/ScrollCapturePreviewPanel.swift:12-46`). On completion the stitched image
goes through the Quick Save route (§12.2), history and the thumbnail.
