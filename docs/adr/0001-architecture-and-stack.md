<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# ADR 0001: Architecture and technology stack

- **Status:** accepted, 2026-09-28; foundation write-back 2026-09-29.
- **Decided by:** the maintainer, on the lead architect's decision record after two adversarial reviews.
- **Scope:** the application architecture, the stack, the platform strategy, the M0 gates, the performance budgets,
  the milestones, the deviation register and the MacShot parity digest.

This ADR is the public summary of the maintainer's architecture decision record (kept privately together with the
research and host probes behind it). The specification in [`docs/spec/`](../spec/README.md) turns these decisions into
contracts; the [PRD](../prd.md) turns them into product requirements. Outcomes of the M0 gates are recorded as later
ADRs that amend this one.

## Context

AriadShot remakes [MacShot](https://github.com/sw33tLie/macshot), a macOS screenshot, annotation and screen-recording
tool, for Linux and macOS. The constraints that shaped every decision:

1. **Fidelity is the product.** Constants, defaults, strings, geometry and state machines come from MacShot's source,
   pinned at commit `b4d4f3a` (2026-09-27); invented behaviour is a defect. MacShot ships often, so every parity number
   refers to that snapshot until a deliberate re-baseline.
2. **Linux Wayland is first-class.** The reference machine runs Hyprland 0.56 on Arch Linux, with a 1920×1200 panel, a
   second display rotated to 1080×1920, an Intel iGPU with VA-API H.264/HEVC encoding and PipeWire.
3. **macOS must work well**, including ScreenCaptureKit capture, `NSPanel` overlays and the menu bar item. Every macOS
   claim is contingent on a Mac test environment (gate G7). The project never holds a paid Apple Developer ID, so macOS
   builds are ad-hoc signed and not notarized (DEV-40).
4. **Licensing.** MacShot grants GPLv3 without "or later", so reused MacShot material, and the combined work, are
   GPL-3.0-only. Apple's SF Symbols and SF Pro cannot be used on Linux. Model *weights* must be permissively licensed;
   non-commercial or AGPL models are excluded from defaults.
5. **Privacy.** No telemetry, local-first processing; network only for user-initiated upload, translation and update
   checks, as in MacShot.
6. **The Wayland security model** forbids passive global input capture, client-side positioning of normal windows,
   window lists on GNOME and clipboard ownership without focus.
7. **The user's configuration is respected.** AriadShot never assumes it owns a key chord and never edits the user's
   compositor configuration without an explicit action.

Non-goals: byte-for-byte compatibility with MacShot's file formats (AriadShot has its own versioned formats with the
same fields, DEV-28); Windows, web or mobile clients, own cloud services, telemetry, and features MacShot lacks;
reproducing MacShot's known bugs (they are fixed and registered); pixel identity between Linux and macOS (each meets
the rendering contract and matches the MacShot corpus within its tolerance class); MacShot's macOS 12.3–13 support.

## Decision

### 1. Chosen stack

| Layer | Choice |
| :--- | :--- |
| Language | C++20 with CMake and Ninja; Objective-C++ and a small Swift module on macOS, Swift only where Apple has no Objective-C API (Translation) |
| UI toolkit | Qt 6 Widgets for ordinary windows; minimum Qt 6.8, the Qt minor pinned per release; an `AriadStyle` proxy style for standard controls on Linux, the native style on macOS |
| Canvas and chrome | host-agnostic C++ view objects (ported MacShot views) and an own canvas text control on `QTextDocument` and `QTextLayout`; popovers and menus inside overlays are chrome views |
| Surface hosts | H1 `QWidget` + `layer-shell-qt` (default), H2 `QQuickWindow` + `layer-shell-qt`, H3 a hand-written layer-shell client, chosen by gate G1; `NSPanel` on macOS; override-redirect windows on X11; fullscreen `xdg_toplevel` on GNOME |
| Renderer | `QPainter` raster on `QImage`, plus AriadShot's own Gaussian blur, calibrated shadows, 3×3 mesh gradients and effect LUTs; canonical images under the still-image contract (§4) |
| GPU | `QRhi` with `QRhiWidget`: Vulkan selected explicitly on Linux with an OpenGL fallback, Metal on macOS (needs `Qt6::GuiPrivate`) |
| Media | FFmpeg (libavformat, libavcodec, libswscale, libswresample) on both systems; PipeWire; ScreenCaptureKit audio and an AVCapture microphone on macOS; libimagequant for GIF |
| Wayland | a second `wl_display` connection that AriadShot owns, on its own thread, for `ext-image-copy-capture-v1`, `zwlr_screencopy_v1`, `ext_data_control_v1`, `hyprland_toplevel_export_v1`, `zwlr_virtual_pointer_v1` and `hyprland_global_shortcuts_v1` (fallback); Qt's connection for layer surfaces, text input, cursor shape and `wp_pointer_warp_v1` |
| Portals and D-Bus | QtDBus: GlobalShortcuts, ScreenCast, Screenshot, FileChooser, OpenURI, Notification, `FileManager1`; restore tokens rotated after each start |
| X11 | xcb: XShm, EWMH, XTest, XInput2 |
| macOS platform | ScreenCaptureKit, Vision, Translation, Speech, Carbon hotkeys, AppKit window levels, `NSStatusItem`, Sparkle 2 |
| Machine learning on Linux | ONNX Runtime (RapidOCR/PaddleOCR, YuNet, SSD or RT-DETR, BiRefNet-lite), zxing-cpp, CTranslate2, whisper.cpp; Tesseract optional; a model catalogue recording each model's weights licence, source and SHA-256 |
| Network | `QNetworkAccessManager`, Qt Network Authorization (OAuth 2 with PKCE and a loopback redirect), an own AWS SigV4 signer |
| Secrets | QtKeychain, with a 0600 file fallback and a visible degraded state |
| Localisation | Qt Linguist `.ts`/`.qm` converted from MacShot's 40 catalogues, runtime retranslation, right-to-left mirroring |
| Testing | Qt Test on the offscreen platform, the parity ledger, MacShot corpus goldens, trace-parity replays, rendering-contract tests, libFuzzer, ASan/UBSan/TSan, headless Sway, nested Hyprland, nested KWin, Xvfb, a GNOME VM (M5), a macOS 26 Mac and a macOS 14 VM |
| Licence | GPL-3.0-only for every file and the combined work, with a `PROVENANCE.md` inventory of MacShot material |

### 2. Why C++ and Qt Widgets, and what would reopen the choice

Seven stacks were scored on *equivalent* architectures: the same backend boundary, one canonical still renderer and one
studio compositor for all of them. Qt 6 with C++ and Widgets scored 4.10 of 5; Qt Quick with the same renderer 3.69; a
Rust core with Qt Quick through CXX-Qt 3.37; Rust with GTK 4 3.29. The lead held under a solo-developer weighting, a
funded-Rust-team weighting and a macOS-heavy weighting, and flips only if memory safety alone carries more than about a
third of the total weight. The margin over Qt Quick rests on documented toolkit capability and single-language
maintenance, not on the unproven overlay host; that host is therefore a gated, swappable adapter (§8, gate G1) with
pre-costed fallbacks, and the Qt Quick host is one of them.

Memory safety is compensated in the acceptance criteria: sanitizer CI, 24-hour fuzzing of every parser of untrusted
input, hardened release builds, pixel loops confined to one audited module behind bounds-checked `std::span`
accessors, and input sizes bounded by MacShot's own limits (16,384 px, 128 MiB, 4 MiB RTF, 2 MiB HTML).

Only three results reopen the stack:

1. the still rendering contract (gate G3) fails in a way specific to `QPainter`;
2. the studio contract (gate G5) fails on both Vulkan and Metal while an equivalent wgpu spike passes;
3. fuzzing in M1–M2 finds memory-safety defects in AriadShot's parsers faster than they can be fixed. The first answer
   to this one is not a stack change: the untrusted-input parsers move into a small Rust static library with a C ABI.

### 3. Component architecture and design rules

The modules and their allowed dependencies are specified in
[`docs/spec/02-modules-and-interfaces.md`](../spec/02-modules-and-interfaces.md); the overview and data flows in
[`docs/spec/01-architecture-overview.md`](../spec/01-architecture-overview.md). The design rules:

1. **`core/` and `render/` never include platform headers.** Everything that differs by desktop goes through a
   `platform/` interface, and a capability registry records what the current session can do, so UI code asks "can I
   snap to windows here?" instead of checking the compositor name.
2. **One pixel source, under contract.** Every pixel that leaves the application (clipboard, file, upload, history,
   pin, drag-out) is a canonical image from `render/`; every surface displays those images and never re-renders
   annotations. The studio's exported frames come from the same `FrameScene` function, shaders and pass list as its
   stage. Both contracts are tested, not assumed.
3. **MacShot's class boundaries are kept where they carry behaviour**: the overlay and editor subclassing with its
   override points, the delegate output API, the two confirm routes, dismiss versus teardown. They live in
   host-agnostic view objects, so a surface host can be swapped without touching them.
4. **Resident daemon.** The process starts at login and owns the tray, hotkeys, clipboard selections and pre-warmed
   overlay surfaces. It holds no screenshot pixels while idle.
5. **Two Wayland connections with fixed ownership.** Qt's connection serves surfaces and input; AriadShot's own
   connection, dispatched on a dedicated thread with `wl_display_prepare_read`, serves capture, data-control, toplevel
   export and synthetic input. Outputs and seats are bound on each connection and matched by name. Gate G4 proves they
   coexist without stalls.

Porting principles: one canonical canvas space (top-left origin, points) with conversions at the edges; one geometry
module for drawing and hit-testing (the sketchy arrow's xorshift32 ported bit-exactly, its seed always persisted); the
attributed text runs are the source of truth for text annotations; a typed settings registry whose defaults are tested
against MacShot's key table and which keeps "unset" apart from "default"; hotkeys stored as platform-neutral
accelerators with an explicit unbound state; colours stored as sRGB values; upstream defects fixed and registered, not
copied; caches are never state.

### 4. Rendering contracts

**Still images** ([`docs/spec/03-rendering-contracts.md`](../spec/03-rendering-contracts.md) §2). Every still is
rendered once into a canonical `QImage` (`ARGB32_Premultiplied`, `QColorSpace::SRgb`, the captured pixel size, device
pixel ratio equal to the capture scale); committed annotations, the in-progress annotation and the text being edited
are separate canonical layers, and chrome is its own layer that is never exported. Inputs are fixed: bundled Inter and
Noto Color Emoji registered explicitly on Linux, hinting off, no subpixel antialiasing, text laid out in canvas points
at a fixed logical DPI. Presentation never re-renders: at 1:1 on an integer-scale output it is a blit, otherwise one
documented filter (nearest at integer zoom ≥ 1, bilinear elsewhere). PNG export equals the canonical image after
un-premultiplication; lossy formats are compared by PSNR and SSIM with thresholds set at G3; the clipboard carries the
same bytes as a file export. "Preview equals export" is therefore a contract with comparison tests, not a claim that
holds by construction.

**Studio** ([`docs/spec/03-rendering-contracts.md`](../spec/03-rendering-contracts.md) §3). An immutable `FrameScene`,
built by one function from a project revision and a composition time, is rendered by one shader set and pass list in
MacShot's layer order, blending in RGBA16F linear space with a final 8-bit sRGB encode. Export renders the exact
rational cadence on its own offscreen `QRhi`; one documented RGB → YUV path (BT.709, limited range) feeds the encoder.
Preview at export resolution must match the pre-encode export within ΔE00 ≤ 1 on 99.9 % of pixels; the decoded export
must reach PSNR ≥ 38 dB and SSIM ≥ 0.97 (hypotheses until G5).

**Recording** ([`docs/spec/05-recording-and-studio.md`](../spec/05-recording-and-studio.md)). The Hyprland recording
source (image-copy with DMA-BUF, or ScreenCast with PipeWire) is decided by measurement in G6. Frames carry
variable-frame-rate timestamps with MacShot's 1 fps heartbeat. The writer produces fragmented MP4 with about 1 s
fragments (one per keyframe) and `fdatasync` at most every 2 s, then publishes a non-fragmented `+faststart` MP4 and
recovers interrupted takes on the next launch (DEV-29).

### 5. Platforms: support tiers and what differs

| Tier | Platforms | Promise |
| :--- | :--- | :--- |
| Reference | Hyprland | full parity with MacShot, measured every milestone on the reference host and in nested Hyprland |
| Reference target, contingent | macOS 14 and later | the same parity bar, at most one milestone behind Hyprland; contingent on gate G7 and the macOS feature gates, otherwise only "builds, launches, captures, copies" |
| Supported, contingent | Sway and other wlroots compositors, KDE Plasma 6, X11 | full parity except the capabilities below, claimed only for what CI measures in nested KWin, nested Sway and Xvfb/Xephyr |
| Best effort | GNOME Shell on Wayland, Flatpak builds | capture, annotate, editor, history and recording through portals; every gap registered; nothing claimed beyond what a GNOME 48+ VM measures |

macOS 14 is a product decision, not a Qt limit: `SCScreenshotManager` and the Vision foreground mask need it, and
supporting 12.3–13 would need a second still-capture path (DEV-27). The minimum is a *tested* minimum: it holds only
while G7's macOS 14 VM passes; otherwise it narrows to the oldest version that passes.

What differs by desktop, and how (the full matrix is in
[`docs/spec/08-platform-integration.md` §2](../spec/08-platform-integration.md#2-capability-matrix)):

| Capability | Hyprland | KDE Plasma 6 | X11 | GNOME Wayland | macOS |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Freeze-frame capture | image-copy, no prompt (G2) | image-copy if allowed, else ScreenCast | XShm | Screenshot portal; may show a dialog (DEV-26) | ScreenCaptureKit |
| Overlay above panels | layer-shell (host by G1) | layer-shell | override-redirect | fullscreen window, no guarantee over panels | `NSPanel` level 257 |
| Window snapping | Hyprland IPC | boundary snapping only (DEV-17) | EWMH | boundary snapping only (DEV-17) | window list |
| Element snapping | off (DEV-16) | off | off | off | Accessibility API |
| Global hotkeys | GlobalShortcuts portal, protocol fallback | portal | `XGrabKey` | portal (GNOME 48+) | Carbon |
| Background clipboard | data-control (G4) | data-control | selection owner | only while focused | pasteboard |
| Click and key telemetry | opt-in input helper (DEV-24) | opt-in helper | XInput2 | opt-in helper | event taps |
| Excluding own HUD from recordings | outline and HUD outside the region; HUD on another output or hidden (DEV-22) | same | same | same | ScreenCaptureKit exclusion |

How degradation is shown: capabilities are resolved at start-up and on display changes from protocol globals, portal
versions and probes, never from the desktop name. A capability never available is hidden (as MacShot hides
macOS-14-only actions); one available after a user action is shown disabled with a one-line reason and a "How to enable"
link; a degraded one shows a dismissible helper badge the first time. A Desktop Integration page in Settings (Linux)
lists every capability with its status, reason and fix, and replaces MacShot's Screen Recording onboarding window
(DEV-30). Every degradation is a deviation-register entry.

### 6. Linux behaviour where MacShot's cannot carry over

#### 6.1 Tab snap cycle and helper card

MacShot cycles window → off → element and prints "Snap mode: WINDOW/ELEMENT/OFF (Tab to switch)". On Linux, Tab skips
unavailable modes (window → off → window on Hyprland, Sway and X11; only "off" on KDE and GNOME Wayland, where Tab does
nothing). The helper card shows MacShot's text for the effective mode and never lines for unavailable modes; with one
mode left it reads "Snap mode: OFF" without "(Tab to switch)". A stored mode that is unavailable is coerced at load to
the next available mode without rewriting the stored value (DEV-15, DEV-16).

#### 6.2 Shortcuts recorder and default chords

On Wayland the portal or the compositor owns the key, so the recorder becomes "request, then read back". With the
GlobalShortcuts portal, the recorded chord is passed as the preferred trigger and the row shows the portal's trigger
description. On Hyprland, "Apply in Hyprland" writes a managed `ariadshot-bindings.lua` and shows the one `require` line
the user adds; AriadShot never edits the user's own files. X11 grabs the chord directly; macOS uses Carbon. Linux
defaults (DEV-13): Capture Area is *requested* as Ctrl+Shift+4; Capture Screen (Ctrl+Shift+3) and Record Area
(Ctrl+Shift+5) are suggested but unbound; every other slot is unbound, because MacShot's ⌘⇧ letters land on common
application chords on Linux and Super+Shift is taken by the desktop. AriadShot never steals a chord silently.

#### 6.3 Request queues without macOS permissions

MacShot holds capture-class URL actions until the Screen Recording permission exists and drops them if onboarding
closes without it. On Linux the capture capability has the states `probing` (hold, at most 2 s), `available` (run),
`consent-pending` (the first request waits for the portal dialog, later ones are held), and `denied` or `unavailable`
(drop, with an error pill and a CLI error carrying the same message; no surprise replay later). Non-capture actions
always run immediately.

#### 6.4 IME fallback disclosure

If gate G1 or a later report shows an input method and compositor combination where in-place text entry fails, only
that combination switches text entry to a popup anchored at the text box, then to the editor window (DEV-21). Enter,
click-outside, Esc and scoped undo keep MacShot's behaviour, and the Desktop Integration page names the combination.

#### 6.5 On-device model downloads

Linux features whose model is missing (Remove Background, Vietnamese OCR, offline translation, captions) ask for
consent first, showing the model, size, weights licence and source; nothing downloads without it. Downloads show
progress on the invoking control and can be cancelled; failures (network, SHA-256 mismatch) show an error pill and leave
the feature disabled with a retry link. The offline build offers "Import model file…" instead. macOS keeps MacShot's
zero-download behaviour through the Apple frameworks (DEV-20, DEV-32).

#### 6.6 Substitutes for macOS-only services

Share is hidden on Linux (no freedesktop share sheet exists); Quick Look opens the pin-style preview; Open With calls
the OpenURI portal so the desktop shows its chooser; the studio's wallpaper background uses the current wallpaper where
the desktop exposes it; there is no global menu, so each window wires Ctrl+W, Ctrl+Q and the edit chords itself
(DEV-12, DEV-18, DEV-19).

### 7. Licensing, packaging, icons and fonts

- **GPL-3.0-only** for the combined work and, by the foundation decisions in
  [ADR 0002](0002-foundation-and-workflow.md), for every file, so no MacShot-derived code can be labelled "or later". `PROVENANCE.md` lists every reused
  MacShot asset: the 40 converted catalogues, gradient, preset, emoji, font-list and PII-pattern tables, ported
  algorithms and copied constants. MacShot's name, logo, DMG art, capture sound, embedded imgbb key and Google OAuth
  client are not reused (DEV-01, DEV-31).
- **Qt module licences** differ: Qt Network Authorization is GPL-3.0 or commercial (compatible, but it removes any
  LGPL-only distribution option); `layer-shell-qt` is LGPL-2.0-or-later; QtKeychain is BSD-3-Clause.
- **FFmpeg per package:** the AUR package links the distribution's GPL FFmpeg; the AppImage bundles an LGPL FFmpeg with
  VA-API and NVENC and downloads Cisco's OpenH264 at run time with consent; the DMG bundles an LGPL FFmpeg and uses
  VideoToolbox. Qt and FFmpeg stay dynamically linked in bundles. A codec patent and distribution review gates the
  AppImage and DMG releases.
- **Distribution:** the AUR package first, then an AppImage, then a portal-only Flatpak. No self-updater on Linux; the
  update check links to the package manager (DEV-36). macOS: an ad-hoc-signed, never-notarized DMG with Sparkle 2 and
  EdDSA signatures, the documented Gatekeeper first-open steps, and no official Homebrew cask (DEV-40).
- **Icons and fonts:** SF Symbols and the system font on macOS; on Linux a Lucide-based icon set (ISC) tuned to SF
  Symbols' weight, checked on a per-icon approval sheet, and bundled Inter (OFL) (DEV-02, DEV-03).

### 8. Gates G1–G8

M0 proves or kills the load-bearing assumptions before feature work. Full pass criteria, lanes and G1 outcomes:
[`docs/spec/14-gates-and-milestones.md`](../spec/14-gates-and-milestones.md) §2.

| Gate | Proves | If it fails |
| :--- | :--- | :--- |
| G1 Overlay host | `QWidget` or `QQuickWindow` surfaces work on Hyprland layer-shell: mapping per output over the bar, one exclusive keyboard surface at a time (the one holding the selection, else the pointer's), popovers inside the surface, Vietnamese and CJK input, re-show latency, frame time, cursor shapes | predefined outcomes O1–O14: H2 for 1–2 weeks, H3 for 4–6 weeks, IME popup fallback (DEV-21), or a fullscreen window as the last resort; no stack change |
| G2 Capture | the capture ordering contract, not just advertised globals: per-output sources, no AriadShot pixels in 100 captures while a thumbnail and a pin are visible, the rotated display upright, buffer renegotiation, fallback to screencopy, cursor per setting, latency measured; synthetic scene first, then a live test | screencopy becomes primary; if both fail, a portal ScreenCast snapshot path and a revised latency budget |
| G3 Still rendering contract | §4 with three tools, a mesh gradient, the calibrated shadow and text with emoji; PNG equals the canonical image; presentation exact at integer scale; lossy thresholds set | fix the inputs; a `QPainter`-specific failure reopens the stack |
| G4 Wayland connections | 10 minutes of captures plus unfocused clipboard copies without a stall longer than one frame; a data-control copy survives overlay dismissal | bind on Qt's connection behind a Qt-version-pinned adapter |
| G5 Studio contract | a decoded FFmpeg frame through `QRhi` (Vulkan) in a `QRhiWidget` and offscreen, encoded with VA-API into fMP4, within the §4 thresholds; the OpenGL fallback runs | fix; a `QRhi`-specific failure on both APIs with a passing wgpu spike reopens the stack |
| G6 Recording path | 1080p60 on image-copy DMA-BUF and on PipeWire: dropped frames, CPU and copies per frame; 200 kill trials | choose the better path; revise the budget with the numbers, never silently |
| G7 macOS environment | a macOS 26 Mac and a macOS 14 VM on it as a self-hosted runner: tests on both, MacShot built at `b4d4f3a` (corpus on 26), the overlay skeleton, ad-hoc inside-out signing that passes `codesign --verify --deep --strict`, the Gatekeeper first-open steps, and whether privacy grants and Keychain access survive a replaced bundle, ad-hoc and self-signed | without a Mac, macOS stays "builds only" and corpus lines have reduced confidence; without the VM, the minimum narrows |
| G8 Baselines | a baseline for every budget of §9 on topologies P1–P3 | each budget is kept, revised or rejected with numbers |

At the end of M0 each outcome is recorded in its own ADR: the host chosen, the recording source chosen, each budget
kept or revised.

### 9. Performance budgets

Targets are hypotheses until G8 records baselines; afterwards each budget is kept, revised or rejected with measured
numbers. Topologies: **P1** the reference host (1920×1200 plus a display rotated to 1080×1920, scale 1); **P2** the
panel at fractional scale 1.25 (nested or headless); **P3** one 3840×2160 output at scale 1 and 2. Every run records
p50 and p95 frame times, resident memory, CPU pixel copies per frame and dropped frames.

| Budget | Workload | Target |
| :--- | :--- | :--- |
| Hotkey → pointer output frozen and interactive | warm daemon, P1, 20 own windows hidden first | ≤ 100 ms p50, ≤ 150 ms p95 (MacShot's best case: under 400 ms) |
| Hotkey → all outputs frozen | same | ≤ 250 ms p95 |
| GNOME hotkey → overlay | GNOME 48+ | measured and documented; no fixed promise |
| macOS hotkey → overlay | the G7 Mac | ≤ MacShot on the same Mac |
| Annotation drag or selection resize | 50 mixed annotations; P1, P2, P3 | ≤ 16.7 ms p95 |
| Copy or confirm → image on the clipboard | 1920×1200 PNG | ≤ 150 ms p95 |
| Idle daemon | 10 minutes idle after 20 captures | ≤ 100 MB resident, 0 % CPU, no overlay buffers retained |
| 1080p60 recording | 10 minutes of a scrolling browser page, VA-API | ≤ 15 % of one core, zero dropped frames, ≤ 1 CPU pixel copy per frame |
| Studio preview | 1080p take, every look enabled | 60 fps p95 on Vulkan |
| Linux package size | AUR package, system Qt and FFmpeg, no optional models | ≤ 40 MB installed |
| Core model bundle | Linux default catalogue | ≤ 40 MB; larger models on demand |
| macOS app bundle | ad-hoc-signed DMG with Qt and FFmpeg frameworks | ≤ 160 MB |

Robustness gates: no crash in 24 hours of scripted capture and annotate cycles under ASan and UBSan; history survives
`kill -9` at any point with zero lost committed entries; at least 95 % of 200 `kill -9` trials of 10-minute recordings
leave a file playable up to the last synced fragment, judged by libavformat and GPAC `MP4Box`; every parser of
untrusted input survives a 24-hour fuzzing run.

### 10. Milestones M0–M5

Every milestone ends with four artifacts: the parity-ledger report per platform, the budgets that apply, the robustness
gates and the updated deviation register. Milestones gate on domains (`shot`, `rec`, `studio`, `release`), not on row
prefixes. The M1 atomic set and each milestone's content are in
[`docs/spec/14-gates-and-milestones.md`](../spec/14-gates-and-milestones.md) §4.

| Milestone | Goal | Hyprland | macOS (contingent on G7) | KDE Plasma 6 / X11 |
| :--- | :--- | :--- | :--- | :--- |
| M0 Foundations | retire the gate hypotheses | measurements and decisions | G7 | — |
| M1 MVP | replace the desktop's screenshot flow on Hyprland; everything outside the M1 set is absent, and a request for it answers with an explicit error | 100 % of the M1 atomic set | builds, launches, captures, copies | best effort |
| M2 Screenshot parity | beautify, effects, recognition, scroll capture, sharing substitutes, uploads, full Settings, 40 locales | 100 % of `shot` lines | the M1 set with native backends | the M1 set |
| M3 Recording parity | the recorder, HUD, audio, crash recovery, telemetry, self-exclusion; no GIF recording (MacShot exports GIF only from the studio) | + `rec` lines | 100 % of `shot` | ≥ 90 % of `shot` |
| M4 Studio parity | the v4.4 studio: project model, inspector, timeline, auto-zoom, captions, MP4 and GIF export | + `studio` lines | + `rec` | + `rec` where possible |
| M5 Release 1.0 | breadth, packaging, updates, offline build, accessibility, licence release gates | 100 % of applicable lines | 100 % of applicable lines | ≥ 95 %; GNOME gaps all registered |

### 11. Deviation policy and register

Parity is always reported against the deviation register. It lists every intentional deviation, platform substitution
and upstream-defect fix, each with MacShot's behaviour, AriadShot's behaviour, the reason and the platforms affected
(A all, L Linux, W Linux Wayland, K KDE, G GNOME, M macOS). A difference from MacShot without an entry is a defect. The
register grows with every decision that changes user-visible behaviour; in M0 it moves into `parity/deviations.jsonl`,
which then becomes its only authoritative copy. The register as accepted with this ADR:

| ID | MacShot behaviour | AriadShot behaviour | Reason | Platforms |
| :--- | :--- | :--- | :--- | :-: |
| DEV-01 | MacShot name, logo, DMG art, capture sound | AriadShot's own brand assets and capture sound | trademark and asset licence | A |
| DEV-02 | SF Symbols at 14 pt medium | Lucide-based icons tuned to SF weight; custom glyphs for gaps; per-icon approval sheet | SF Symbols licence forbids non-Apple use | L |
| DEV-03 | SF Pro; 18 curated fonts, mostly Apple and Microsoft families | Inter bundled; a per-platform curated list with metric-compatible fallbacks | font licences and availability | L |
| DEV-04 | Apple Color Emoji | Noto Color Emoji | font licence | L |
| DEV-05 | `CIPhotoEffect*` presets | LUTs fitted to corpus renders | Apple internals not published | L |
| DEV-06 | Vision OCR, faces, people, foreground mask; Speech captions; Apple Translation | ONNX Runtime models, zxing-cpp, whisper.cpp, CTranslate2; results differ | platform ML | L |
| DEV-07 | Re-editing text then undoing removes the text; the original is not restored | undo restores the original text annotation | upstream defect | A |
| DEV-08 | Crop and rotate undo do not move annotations back | annotations move back with the image | upstream defect | A |
| DEV-09 | Custom colour slots are saved but never reloaded | slots persist across captures | upstream defect | A |
| DEV-10 | `beautifyBgRadius` is stored, but every beautify configuration passes a background radius of 0, so it is ignored in preview *and* export (`macshot/UI/Overlay/OverlayView.swift:524`, `macshot/Model/CaptureEditState.swift:53`) | the setting is honoured in preview and export; corpus fixtures use 0 | upstream defect | A |
| DEV-11 | The countdown guard can leave a capture stuck when no screen is available | the capture gate is released | upstream defect | A |
| DEV-12 | Standard main menu with ⌘ chords | per-window Ctrl chords; no global menu | no global menu on Linux | L |
| DEV-13 | Global hotkey defaults ⌘⇧X/F/R/H/T/S | only Capture Area requested (Ctrl+Shift+4); Ctrl+Shift+3 and Ctrl+Shift+5 suggested; the rest unbound | application chord conflicts; desktop Super bindings | L |
| DEV-14 | The recorder registers the recorded chord directly | request, then read back from the portal or compositor | Wayland shortcut model | W |
| DEV-15 | Tab cycles window → off → element | unavailable modes skipped; suffix hidden when only one mode remains | capability | L |
| DEV-16 | Element snapping through Accessibility | unavailable | no usable AT-SPI geometry on Wayland | L |
| DEV-17 | Window snapping everywhere | boundary snapping only | no window list for clients | K, G |
| DEV-18 | Share sheet, Quick Look, Open With; labels that name Finder | Share hidden; pin-style preview; OpenURI chooser; platform-neutral "file manager" wording | no Linux equivalents | L |
| DEV-19 | `.wallpaper` studio background from macOS wallpapers | the user's current wallpaper where readable, else hidden | Apple wallpapers cannot be shipped | L |
| DEV-20 | No model downloads | consented on-demand downloads | Linux models are not part of the OS | L |
| DEV-21 | In-place text editing on the overlay | pop-up or editor-window text entry for named IME and compositor combinations only | compositor or IME defect (conditional) | W |
| DEV-22 | HUD excluded from full-screen recordings | HUD on another output or hidden; tray and hotkey stop | no per-surface capture exclusion | L |
| DEV-23 | Webcam bubble, click ring and keystroke pill visible to the user but excluded from editable-mode recordings (baked only in normal mode; CR-23, `macshot/Capture/RecordingEngine.swift:247,316`) | each is hidden where it would lie over the recorded region; the camera track and the telemetry still record | no per-surface capture exclusion | L |
| DEV-24 | Clicks and keystrokes captured without extra setup | needs the opt-in input helper on Wayland; options disabled with a reason otherwise | Wayland input security | W |
| DEV-25 | Auto-scroll capture everywhere | unavailable without input injection | no RemoteDesktop portal or virtual pointer | K, G |
| DEV-26 | Overlay above everything, instant, no dialog; thumbnails and pins on top | fullscreen windows, possible portal dialog, no on-top guarantee, clipboard only while focused, no tray without the extension | Mutter protocols | G |
| DEV-27 | macOS 12.3+ | macOS 14+ | one capture path; macOS 14 APIs | M |
| DEV-28 | Apple plist, reference-date JSON, `cursor.mstl` | own versioned formats, ISO-8601 dates, same fields and semantics | cross-platform formats | A |
| DEV-29 | 10 s movie fragments; telemetry forced to stable storage every 10 s to match (`macshot/Capture/CursorTelemetryRecorder.swift:36-37`) | about 1 s fragments (one per keyframe) with `fdatasync` at most every 2 s; telemetry synced on the same timer | better crash recovery | A |
| DEV-30 | Screen Recording onboarding window | Desktop Integration first-run window with Linux capability content | no TCC on Linux | L |
| DEV-31 | Built-in imgbb key and Google OAuth client | AriadShot's own credentials, or "not configured"; user keys supported | MacShot's credentials are not reusable | A |
| DEV-32 | The offline build removes only upload and cloud integrations; it keeps update checks against its own feed (upstream `.github/workflows/build-release.yml:141,151`) | also disables model downloads (sideload instead) | the offline variant carries no download path beyond update checks and user-initiated translation | L |
| DEV-33 | S3 keys in user defaults, Drive token in a 0600 file | keyring; a 0600 file with a visible degraded state only without a keyring | security improvement | A |
| DEV-34 | Cross-display stitching truncates to the primary scale | per-output native buffers with the mixed-DPR rule | correctness improvement | A |
| DEV-35 | Accessory app with Dock icon switching and a Dock menu | normal toplevels while windows are open; no Dock menu | no Dock on Linux | L |
| DEV-36 | Sparkle auto-updates with a beta channel | package manager plus a check-only notice | Linux packaging norms | L |
| DEV-37 | Translation engine section shown only with Apple Translation | the Linux engines are shown instead | platform ML | L |
| DEV-38 | AppKit standard controls in Settings and the OCR window | `AriadStyle` controls within the geometry-parity gate | no AppKit on Linux | L |
| DEV-39 | Committing an empty re-edit of a text annotation deletes the original, with no undo entry | the annotation is still removed, as one undo step that restores the original | upstream defect | A |
| DEV-40 | Developer-ID-signed, notarized and stapled DMG (upstream `.github/workflows/build-release.yml:62-93, 291-394`); Homebrew cask `macshot` (upstream `README.md:40-43`); a stable signing identity, so privacy grants and Keychain access carry across updates | ad-hoc-signed, never-notarized DMG; Gatekeeper first-open steps; no official Homebrew cask (optional own tap, owner-approved); Sparkle EdDSA-only updates; whether Screen Recording, Accessibility and Keychain access survive an update is measured in G7 and at macOS feature gate 7, and a self-signed project certificate is the owner's option if they do not | no paid Apple Developer ID, by owner decision ([ADR 0002](0002-foundation-and-workflow.md)) | M |
| DEV-41 | Manual scroll capture reacts to global scroll-wheel events: a capture every 0.15 s while scrolling, settled 0.25 s after scrolling stops (`macshot/Capture/ScrollCaptureController.swift:100,104`) | the same cadence and settle rule, driven by frame changes of the captured region | other clients' wheel events are not delivered on Wayland | W |
| DEV-42 | High-quality studio export uses `AVAssetExportPresetHighestQuality`, whose bitrate Apple chooses (`macshot/UI/Editor/Video/VideoEditorExporter.swift:98`) | MacShot's own encoding plan at quality High, through FFmpeg, on both platforms | one encoder implementation on both platforms ([ADR 0001](#1-chosen-stack)) | A |
| DEV-43 | The keystroke pill and keystroke looks show ⌃ ⌥ ⇧ ⌘; the shortcuts-only filter keys on ⌘, ⌃ and ⌥ (`macshot/Capture/KeystrokeTimeline.swift:40,47,78-81`) | Linux key names (Super, Alt, Ctrl, Shift); the filter keys on Super, Ctrl and Alt | Linux keyboard conventions | L |
| DEV-44 | The 18 mesh gradient styles exist only on macOS 15+ and come first in the list, so on macOS 14 the list has 30 styles and indices shift (`macshot/Services/BeautifyRenderer.swift:96-98`) | all 48 styles on every platform, one index space | mesh gradients are drawn by AriadShot's own renderer ([03](../spec/03-rendering-contracts.md)) | M |
| DEV-45 | PII patterns (`\d` in `NSRegularExpression`) and the planner's digit test (`CharacterSet.decimalDigits`) accept any Unicode decimal digit (`macshot/Services/AutoRedactor.swift:27-40`, `macshot/Services/PIIRedactionPlanner.swift:143`) | ASCII digits only, on every platform | identical results on both platforms from one regex engine | A |
| DEV-46 | The `open?file=` URL action always opens the image editor, so a video path fails silently (`macshot/AppDelegate.swift:2396-2425, 2457`) | the SH-30 file router applies: videos open in the studio, images in the image editor | one open route for every entry point | A |
| DEV-47 | SH-29: a one-line SIGTERM diagnostic written with async-signal-safe calls to `~/Library/Logs/macshot/termination.log`, to tell memory-pressure kills from normal quits (`macshot/AppDelegate.swift:89-112, 654-664`) | macOS keeps it (as `AriadShot/termination.log`). On Linux SIGTERM is a normal clean quit; fatal signals append one line (signal, version, UTC time) and raw return addresses to `$XDG_STATE_HOME/ariadshot/logs/crash.log` (0600), then re-raise; no memory, registers or user data | no Jetsam on Linux; SIGTERM is the normal session-end signal | L |

## Consequences

Accepted trade-offs:

- **C++ rather than Rust** for AriadShot's own code gives the smallest integration surface with Qt and every
  dependency, paid for with the sanitizer, fuzzing, hardening and parser-isolation measures of §2.
- **A CPU raster canvas** is faithful to MacShot and deterministic; performance depends on damage-based repainting, and
  texture presentation through host H2 is the prepared escape hatch (it changes presentation, not pixels).
- **Qt Widgets for ordinary windows** keeps the port structural and the daemon free of a JavaScript engine; the few
  MacShot animations are checked against frame-timed budgets.
- **Linux standard controls approximate AppKit** through `AriadStyle`, bounded by a geometry-parity gate (DEV-38);
  custom-drawn UI, the majority, is exact everywhere.
- **GNOME is best effort**; the capability registry makes each gap visible (DEV-26).
- **Two machine-learning stacks** (Apple frameworks on macOS, open models on Linux) give macOS zero downloads at the
  cost of some result differences (DEV-06); PII planning, scroll stitching and all geometry stay shared.
- **FFmpeg on macOS** gives one writer implementation at the cost of bundling LGPL FFmpeg frameworks.
- **A second Wayland connection** keeps ownership simple at the cost of binding outputs and seats twice.
- **GPL-3.0-only** follows MacShot's actual grant; the combined work can never move to a later GPL unless upstream
  grants it.

Top risks and where they are retired: IME in the overlay's text control and QWidget behaviour on layer-shell (G1);
hotkey-to-overlay latency (G1, G8); the CPU canvas at 4K (G1 step 6, M1); passive click and key telemetry on Wayland
(opt-in helper, M3); GNOME overlay parity (never full, disclosed); `QRhi` changes between Qt minors (pinned per release,
G5); Apple-only looks on Linux (fitted LUTs, icon sheet, registered substitutions, M2); two Wayland connections stalling
each other (G4); no Mac or no macOS 14 VM (G7); a missed licence obligation (`PROVENANCE.md`, the model catalogue, the
codec review before M5); the image-copy recording path (G6).

## Evidence and history

- MacShot source at [`b4d4f3a`](https://github.com/sw33tLie/macshot/tree/b4d4f3a) is the authority for every parity
  value; the digest below cites it as `macshot/<path>:<line>` where a row was checked against the source.
- The private decision record behind this ADR went through three revisions: the first draft; a second revision that
  resolved every required change of an adversarial technical review and a fidelity review (equivalent-architecture
  rescoring, gate G1 with fourteen costed outcomes, explicit rendering contracts, the deviation register and the parity
  ledger); and a third revision that recorded the maintainer's decision against a paid Apple Developer ID, the
  deviation entries DEV-40 to DEV-47, the removal of GIF recording from M3 and the G1 keyboard-focus rule. Research
  reports, host probes and working notes stay private because they contain host details.

## Appendix: MacShot parity digest

Each row is a parity item at MacShot `b4d4f3a`. Coarse rows marked **[ledger]** expand into one parity-ledger line per
observable item (a menu item, a key, a settings key); the ledger holds the MacShot value of every line. Tags: **PORT**
(portable as-is: logic, drawing or data), **BACKEND** (needs a platform backend but is achievable on every target),
**WL-HARD** (only some compositors expose the capability, or it needs an opt-in helper; degradation is shown),
**MAC-ONLY** (a macOS concept; Linux gets a registered substitute or nothing). Row counts: SH 34, OV 36, TB 15, AN 24,
CR 24, VE 15, ED 12, SV 8, ST 7 (175 rows).

### A.1 Application shell and lifecycle (SH)

| ID | Item | Tag |
| :--- | :--- | :--- |
| SH-01 | Single instance: a second launch signals the primary (show icon + open Settings) and exits | BACKEND |
| SH-02 | Launch order: migrations, save-failure toast wiring, cleanup on a background queue, history init with orphan prune, updater, menus, status item, hotkeys, audio pre-warm, workspace/display/keyboard-layout observers, permission check, overlay pre-warm | PORT |
| SH-03 | Two request queues: cold-launch queue drained after startup; capture-class queue held until capture permission exists and **dropped** if onboarding closes without permission | PORT (Linux states in §6.3) |
| SH-04 | URL scheme `macshot://` actions capture, capture-fullscreen, capture-last, quick-capture, ocr, ocr-translate?target=, record, record-fullscreen, scroll-capture, settings, history, open?file=, edit?id=, stop-recording; `urlSchemeEnabled` true | BACKEND |
| SH-05 | Accessory app with no Dock icon; becomes a regular app while the image editor, video editor or Settings is open; returns focus to the previous app on close; never hides the app | MAC-ONLY (Linux: normal toplevels, focus return is compositor-specific; DEV-35) |
| SH-06 | Custom Dock menu listing windows alphabetically, miniaturized shown as mixed | MAC-ONLY (DEV-35) |
| SH-07 | Status item: 22×22 pt template icon, default asset or custom symbol name, `hideMenuBarIcon`; during recording forced visible, becomes `stop.circle.fill`, click stops recording, menu detached | BACKEND (SNI; GNOME needs AppIndicator extension) |
| SH-08 | **[ledger]** Status menu: 6 reorderable capture items (Capture Area ⌘⇧X, Capture Screen ⌘⇧F, Capture OCR & QR ⌘⇧T, Quick Capture ⌘⇧S, Capture Last Area, Scroll Capture), Capture Delay None/3/5/10/30 s, Record Area ⌘⇧R, Record Screen, Recent Captures submenu ("W × H · time ago" with thumbnail, click copies, Clear History with confirm, empty text), Show History Panel ⌘⇧H, Open Image…, Open Video…, Show Recordings in Finder, Open from Clipboard, Pin from Clipboard, Settings… ⌘,, Check for Updates…, Quit | BACKEND |
| SH-09 | Menu shortcut labels hide for disabled hotkeys and are rebuilt when the keyboard layout changes | BACKEND |
| SH-10 | **[ledger]** 12 global hotkey slots with defaults ⌘⇧X/F/R/H/T/S, others unset; F-keys may be bare; per-slot disable flag; opening a hotkey closes modal windows | WL-HARD (portal, Hyprland protocol, compositor binds, X11 grab, Carbon; Linux defaults and recorder in §6.2) |
| SH-11 | **[ledger]** 27 single-key overlay/editor shortcuts (26 offline), layout-aware character matching, empty = disabled | BACKEND (layout-aware matching needs xkbcommon keymap translation, as SV-08) |
| SH-12 | Editor undo ⌘Z, redo ⌘⇧Z and ⌘Y, customizable chords that require ⌘ and steal conflicting chords | PORT (Ctrl on Linux) |
| SH-13 | Capture gate (one capture at a time, not while recording), session ID discards stale callbacks, previous-app and window-title capture, tool reset when `rememberLastTool` is false, own windows hidden during capture | PORT (title: BACKEND) |
| SH-14 | Pre-capture countdown: 140×140 floating window centred on screen (on the selection for recording, mouse-transparent), 120 pt circle black 0.7, 3 pt ring white 0.6, 52 pt mono-digit bold; Esc cancels via a local monitor and clears 11 pending flags | PORT |
| SH-15 | Overlay pool: one pre-warmed overlay per display kept alive between captures, rebuilt on display change | BACKEND |
| SH-16 | Progressive multi-display capture: pointer display first and interactive immediately | WL-HARD (the GNOME Screenshot portal returns one whole-desktop image, so it cannot be pointer-display-first) |
| SH-17 | Two output routes: Confirm (Copy button, ⌘C, double-click) always copies; Quick Save (Return/Enter, quick-capture mouse-up) follows `quickCaptureMode` 0 save, 1 copy (default), 2 save+copy, 3 nothing, 4 save+copy path; optional open editor | PORT |
| SH-18 | Quick Capture: toolbars hidden, mouse-up saves | PORT |
| SH-19 | Quick OCR: mouse-up runs text+QR recognition; `ocrAction` 0 window+copy, 1 window, 2 copy | PORT (engine: BACKEND) |
| SH-20 | OCR-translate URL action: OCR, translate, draw translated text in place | PORT (engines: BACKEND) |
| SH-21 | Repeat last area: saved rect applies only to a display with an identical frame | PORT |
| SH-22 | Permission onboarding window (400×520 / 610 returning user, 0.75 s polling, deep link, auto-advance 1 s) | MAC-ONLY (Linux: Desktop Integration first-run window, §5; DEV-30) |
| SH-23 | Settings export/import: JSON envelope, fail-closed secret filter, machine-key exclusions, 2 MiB data cap, replace-portable import | PORT |
| SH-24 | Launch cleanup sweepers with TTLs (editor clones, tmp 24 h, share scratch 5 min, legacy dirs) | PORT |
| SH-25 | Offline build: removes upload and cloud only; update checks remain | PORT (build flag) |
| SH-26 | Updates: signed appcast, daily check, no auto-download, beta channel | MAC-ONLY (Sparkle); Linux via package manager + check-only notice (DEV-36) |
| SH-27 | 40 locales + system, ~858 keys, runtime language switching | PORT |
| SH-28 | Capture sound with pre-warmed audio output; `playCopySound` true | BACKEND |
| SH-29 | SIGTERM diagnostic log, App Nap suppression | MAC-ONLY (Linux: crash log only) |
| SH-30 | File associations and open routes. MacShot is an alternate Editor for PNG, JPEG, TIFF, GIF, HEIC, HEIF, WebP and BMP (`macshot/Info.plist:24-45`). The router sends image extensions (including `.icns`) to the image editor, `mp4`/`mov`/`m4v` to the studio, and GIF deliberately to the image editor (`macshot/AppDelegate.swift:2389-2428`). Entry points: Open With, Dock drop, command line, Open Image…, Open Video…. The studio opens a non-take video without telemetry, with cursor, zoom-from-clicks and keystroke looks disabled and the reason shown | BACKEND (Linux `.desktop` `MimeType=` list plus the CLI; macOS `CFBundleDocumentTypes`) |
| SH-31 | Standard main menu: App (About, Quit ⌘Q), File (Close Window ⌘W), Edit (Undo and Redo with dynamic chords, Cut, Copy, Paste, Delete, Select All) (`macshot/AppDelegate.swift:672-714`). Settings, the OCR window and the editor depend on it for standard ⌘ shortcuts | MAC-ONLY (substitute: Linux has no global menu, so each window wires Ctrl+W, Ctrl+Q and the edit chords itself; DEV-12) |
| SH-32 | Deferred restoration of AriadShot's own windows across overlapping capture cycles, guarded by a generation token, so stale windows never flash into the next capture (`macshot/Services/DeferredRestoration.swift:1-34`) | PORT (hide and restore mechanics: BACKEND, the capture ordering contract of docs/spec/04-capture-and-overlay.md §4) |
| SH-33 | On-demand model downloads (Linux only; MacShot has none because it uses Vision, Speech and Translation). Needed by Remove Background, Vietnamese OCR, offline Translate and captions. Includes consent with size and licence, progress, cancel, failure pill, the offline-build behaviour, and model management in Settings (§6.5) | BACKEND (new flow; DEV-20) |
| SH-34 | **[ledger]** "Show in file manager" routes: the status menu's Show Recordings in Finder (`rec`, M3), and the ED-06 and history reveal actions (`shot`, M2) | BACKEND (Linux `org.freedesktop.FileManager1.ShowItems` over D-Bus, else open the folder through the OpenURI portal) |

### A.2 Capture overlay (OV)

| ID | Item | Tag |
| :--- | :--- | :--- |
| OV-01 | One overlay view per display at the top window level, covering panels; frozen screenshot redrawn, dim black 0.45 outside the selection (`disableSelectionOutsideShadow`) | BACKEND (layer-shell; GNOME: fullscreen toplevel) |
| OV-02 | States idle / selecting / selected; flags editor, scroll-capturing, recording, auto OCR, auto quick-save, auto record, auto translate | PORT |
| OV-03 | Delegate output API of about 29 callbacks (confirm, save, save as, pin, OCR, quick save, upload, share, remove background, record, scroll capture, permissions, remote selection sync, snap mode sync, add capture, restore last) | PORT |
| OV-04 | Idle helper card: centred, black 0.65, r 8, pad 14, 13 pt medium line per snap mode, "Snap mode: WINDOW/ELEMENT/OFF (Tab to switch)" with green/orange state; `hideCaptureInstructions` | PORT |
| OV-05 | Selecting badge: "Hold Space to move. Release to annotate and edit" (quick-save variant), 12 pt medium, r 6, 8 pt below with flip, ±4 clamp | PORT |
| OV-06 | Pre-selection preset button 34×28 in the idle card: freeform / ratio / exact resolution, persisted, cleared when resized away | PORT |
| OV-07 | Tab cycles snap mode window → off → element (persisted `captureSnapMode` 0/1/2 = window/element/off), syncs all displays | PORT (Linux cycle and card per capability: §6.1) |
| OV-08 | Window snapping: front-to-back window list, normal layer only (Quick Look exception), > 10×10, Finder preview heuristic; highlight systemBlue 0.08 fill / 0.85 2 pt r 4; click selects the window and grabs a separate alpha-cornered window image for beautify | WL-HARD (Hyprland/Sway IPC, X11 EWMH, macOS; KDE best effort; GNOME none) |
| OV-09 | Element snapping via accessibility tree (depth 8, 48 nodes, 64 children, 35 ms), Chromium enhancement, retries 0.1/0.5/2.1 s | MAC-ONLY (Linux AT-SPI best effort, off by default) |
| OV-10 | Boundary snap index: per-pixel RGB Euclidean neighbour differences, off-thread, skip > 40 MP; query ±round(4 pt·scale), mean ≥ 28 and support ≥ 0.55, nearest wins; suppressed by Option, active ratio preset, Shift, Space; accent 0.9 1 pt full-screen guides | PORT |
| OV-11 | Auto-adjust selection (radius min(160, max(48, 0.3·dim)), central 15% inset, min 4 pt) | PORT |
| OV-12 | Alignment snap 5 pt to selection and annotation min/mid/max; cyan 0.6 dashed [4,3] guides | PORT |
| OV-13 | Auto-measure while holding 1 (vertical) or 2 (horizontal): L1 RGB ≥ 30 edge scan; clamp to selection when inside (`measureClampToSelection` default **true** (`macshot/UI/Overlay/OverlayView.swift:580`)) | PORT |
| OV-14 | Colour sampler: 1×1 sRGB unpremultiplied, "#RRGGBB" pill (black 0.85, r 6, mono 12, 16 pt swatch, "Right-click to copy"), writes custom colour slots | PORT |
| OV-15 | Selection chrome: 2 pt accent border (2.5 pt red in scroll capture), eight 10 pt handles with 14 pt hit area, 6 pt edges excluding corners, cursor waterfall including an invisible cursor under the pencil dot | PORT (cursor shapes: BACKEND) |
| OV-16 | Modifiers: Shift 1:1, Option from centre, locked aspect maths, Space rigid move, arrows 1 pt / Shift 10 pt, right-click anchored selection | PORT |
| OV-17 | Resolution box: 166 + glyph width × 34, W and H fields 56×22 (1…100,000), "×" 13 pt, presets button (accent when active), placement candidates above/below/inside/least overlap, notch avoidance, px/pt toggle, ratio label rules, typed-size maths | PORT |
| OV-18 | Ratio presets 9 ratios + freeform + live custom; 7 fixed sizes; keep ratio for next captures; Auto Adjust | PORT |
| OV-19 | Right-click radial colour wheel: 16 swatches, ring r 72, swatch r 12 (15 hover), dead zone 18, drag or sticky pick | PORT |
| OV-20 | Double-click inside rewinds first-click annotations then confirms; outside confirms; outside single click is inert; on text edits text | PORT |
| OV-21 | **[ledger]** Keys: Tab, F (full display when snap ≠ off; saves in quick mode), R (restore last in idle), Esc priority waterfall (7 levels), Return/Enter quick save, Delete, ⌘C/⌘V/⌘D (+15, −15), ⌘Z/⌘⇧Z, ⌘S (per save action), ⌘⇧S, hold 1/2 | PORT |
| OV-22 | Multi-display: global coordinates, remote selection border and handles on other displays, resize from any display | PORT |
| OV-23 | Cross-display stitching (MacShot truncates to the primary scale; AriadShot must use per-output native buffers and the two-branch mixed-DPR rule) | PORT (improved; DEV-34) |
| OV-24 | Draw order of 24 passes (freeze/dim, snap highlight, helper, remote selection, cutout, cached annotations, in-progress, auto-measure, crop preview, loupe preview, sampler pill, annotation chrome, drawing dot, guides, lasso, beautify preview re-drawing overlays, effects preview, border, handles, boundary guides, text chrome, stamp hover, error pill, tooltip) | PORT |
| OV-25 | Beautify live preview with toolbar anchor animation (60 Hz, +0.08/tick, ease-out) | PORT |
| OV-26 | Effects live preview clipped to the selection | PORT |
| OV-27 | Error pill (red 0.8/0.2/0.2/0.9, r 8, 13 pt, 40 pt below top, 4 s) | PORT |
| OV-28 | Finish flows: confirm (effects then beautify, window-snap annotations on the window image, always clipboard), quick save, share (0.5 s re-entry throttle, overlay lowered while the picker is open), save as (cancel restores the overlay), file save (sound after write), pin, OCR, upload (online build) | PORT; share: MAC-ONLY (substitute: no freedesktop share sheet exists, so Share is hidden on Linux; DEV-18) |
| OV-29 | Dismiss keeps the overlay alive and releases images; teardown only on display change or quit; last selection saved when larger than 1×1 | BACKEND |
| OV-30 | Recording outline: click-through outward 1.5 pt accent 0.8 stroke outside the crop | BACKEND |
| OV-31 | Recording popover (240 wide, session-only FPS 15/30/60/120, When done, Delay, Hide controls; persistent webcam position/size/shape) and other popovers (upload confirm, redact categories, translation languages filtered to installed Apple languages, gradient grid, effects) | PORT |
| OV-32 | Undo entries added / deleted(index) / image transform (previous image, window image, offsets) / property change | PORT |
| OV-33 | Editor transforms: flip H/V with annotation mirroring, add capture below with canvas auto-expansion by opaque scan, invert, crop commit | PORT |
| OV-34 | Zoom transforms canvas↔view | PORT |
| OV-35 | Scroll-capture state in overlay: transparent hole, red border, HUD | BACKEND |
| OV-36 | Hit-test pass-through rules for chrome and hidden pooled chrome | PORT |

### A.3 Toolbars and tool options (TB)

| ID | Item | Tag |
| :--- | :--- | :--- |
| TB-01 | Solid fills only (no blur or vibrancy); theme accent #8C4DD9, icon white, background #1F1F1F; controls light when background brightness > 0.5 | PORT |
| TB-02 | Theme presets Default, Classic, Ocean, Sunset, Forest, Mono, and Custom auto-selected when colours differ (±0.01) | PORT |
| TB-03 | Strip geometry: 32×32 buttons r 6, padding 4, spacing 2, strip r 6; gap clicks swallowed in overlay, passed through in editor | PORT |
| TB-04 | Button states pressed accent 0.6, on accent, hover icon 0.12; hover edge cases (strip exit clears, sibling clear, suppression during and 3 pt after drags); click only on mouse-up inside; move button forwards drags | PORT |
| TB-05 | Icons 14 pt medium tinted, cache; custom checkerboard; swatch button; context triangle; mic level fill green 0.45 | PORT (icon set: see §7) |
| TB-06 | Tooltips instant, 11 pt medium, pad 6/3, r 4, 4 pt above/below or 6 pt left, shortcut suffix only with `showToolShortcutsInTooltips` (default false) | PORT |
| TB-07 | Bottom bar order: pencil, line, arrow, rectangle, ellipse, marker, text, number, censor, highlight, loupe, stamp, colour picker, measure, colour, undo, redo, then optional invert, adjust, beautify, remove background | PORT |
| TB-08 | Right bar (capture): cancel, move, open in editor, copy, save (right-click Save As / Save to folder), then optional share, upload, pin, OCR, translate, scroll capture, record | PORT |
| TB-09 | Right bar (recording setup): start (red), cancel, click highlight, keystrokes (menu), system audio, microphone (level + device menu), webcam (device menu), settings popover, move | PORT (devices: BACKEND) |
| TB-10 | Customisation: `enabledTools` (17 values), `enabledActions`, migration guards so disabled items stay disabled | PORT |
| TB-11 | Options row: 34 tall, pad 8, r 6, min width 200, separators 1×18 (0.1) advancing 13; above the bottom bar in the editor, 2 pt below it in the overlay; edits apply to the selected annotation with one undo snapshot committed on deselect | PORT |
| TB-12 | **[ledger]** Per-tool rows (pencil, line, arrow, rectangle, ellipse, marker, text, number, censor, highlight, loupe, stamp, measure, beautify) with exact controls, ranges, procedural segment icons and defaults | PORT |
| TB-13 | Placement algorithm: right bar fit tests with 50 pt margin, narrow-selection fallback, bottom bar below/above/inside, overlap resolved by moving the right bar, resolution-box and notch avoidance | PORT (notch → reserved zones) |
| TB-14 | Popovers: colour picker 186×312 (12 presets, 7 custom slots, opacity, HSB, brightness, display-only hex), font picker (18 curated + system), emoji picker (5 categories, 104), beautify picker, effects picker 250×317, recording settings | PORT (curated fonts: BACKEND) |
| TB-15 | Right-click menus: Save, Keystrokes (Shortcuts Only / All Keystrokes), Mic (None + devices), Webcam (None + devices), Upload confirm, Translate language | PORT |

### A.4 Annotation model and tools (AN)

| ID | Item | Tag |
| :--- | :--- | :--- |
| AN-01 | 19 tool kinds with fixed raw values: pencil 0, line 1, arrow 2, rectangle 3, filledRectangle 4, ellipse 5, marker 6, text 7, number 8, pixelate 9, blur 10 (legacy), measure 11, loupe 12, select 13, translateOverlay 14, crop 15, colorSampler 16, stamp 17, highlight 18 | PORT |
| AN-02 | Vector-first annotation object with cached rasters (baked censor, text image, glow), clone, move, bounding rect, hit test (8 pt threshold, rotation-aware) | PORT |
| AN-03 | Arrow: head length min(max(14, 5w), max(4, 0.45·L)), π/6 heads; styles single, thick (rounded banner), double, open, tail dot, sketchy (xorshift32 seeded, regenerated for legacy 0); straight, legacy control-point bend (a cubic with both control points equal), multi-anchor Catmull-Rom with right-click waypoints; flip | PORT |
| AN-04 | Rectangle and ellipse: stroke / stroke+half-alpha fill / fill (outline w+6 underneath), radius 0–30 clamped to half the short side, solid/dashed/dotted with fitted per-side dashing and Ramanujan perimeters; outline forces solid | PORT |
| AN-05 | Pencil: Chaikin 2 iterations (Smooth), moving average 8 + Chaikin (Refined), pressure width max(w(0.2+p^0.6·0.8), 0.5). Shift locks to horizontal or vertical once the pointer is more than 5 pt (`hypot > 5`) from an anchor; the anchor is the last point when Shift went down, and the lock resets when Shift is released (`macshot/UI/Tools/PencilToolHandler.swift:42-69`) | PORT (pressure input: BACKEND) |
| AN-06a | Plain marker: alpha 0.35, width ×6, Catmull-Rom; Shift is the same either-axis lock as the pencil (AN-05) | PORT |
| AN-06b | Smart marker: always horizontal, anchored at the stroke start with or without Shift; snaps to OCR lines (y = min + 0.55H, width (H+4)/6) | PORT (OCR: BACKEND) |
| AN-07 | Text: System 20 pt (8–200), bold/italic/underline/strike, align L/C/R, background pill (−4, r 4), outline pill, outside-only glyph stroke, committed as rich text plus a high-DPI image; scoped typing undo; Enter newline, click outside commits, Esc cancels | PORT (IME: BACKEND, WL-HARD on overlay surfaces; fallback and disclosure in §6.4, DEV-21) |
| AN-08 | Number badge R = 8 + 3w, bold 1.1R, luma rule, pointer cone (base 0.55R), formats decimal / roman 1–3999 / A–Z / a–z wrap; next = max + 1 or start value (a separate undo-tracked counter also exists; the supplement flags the interaction) | PORT |
| AN-09a | Censor drawn by hand: pixelate (÷8 then ÷2, nearest up), blur σ = max(10, 0.03·min(w,h)) edge-clamped, solid, erase (bilinear edge interpolation, 4 px pad, ≤ 3 px samples) | PORT |
| AN-09b | Censor "Draw: Text Only" mode, restricted to OCR boxes (finishing runs redact-all-text as one undo group) | PORT (OCR: BACKEND) |
| AN-10 | Auto-redact: All Text, PII (11 categories over OCR text, multi-observation card joining, contextual CVV), Faces, People | PORT (detectors: BACKEND) |
| AN-11 | Loupe: 40–320 (120), 1.1–6× (2), two shadows, 4 pt gradient ring or solid outline, rooted two-circle mode with leader line | PORT |
| AN-12 | Measure: px/pt, "{D}{unit} ({W} × {H})" rules, mono semibold 11 on black 0.75, ticks ±6 | PORT |
| AN-13 | Highlight/spotlight: dim 0.1–0.95 (0.55) over the union of holes, dashed border default (key `highlightBorderDashed` (`macshot/UI/Tools/HighlightToolHandler.swift:16`)) | PORT |
| AN-14 | Stamp: emoji or image, size 16–256 (64), 17 quick emojis, 5-category picker, custom PNG/JPEG/WebP/SVG, capture stamps from Add Capture/paste | PORT (emoji font: BACKEND) |
| AN-15 | Translate overlay annotation: average background colour, contrast text, font shrink to fit | PORT |
| AN-16 | Crop tool (editor only) and colour sampler tool | PORT |
| AN-17 | Selection chrome: 10 pt accent handles, 8 pt bend handles, 22 pt rotation handle 20 pt above (Shift 45°), delete and text-edit buttons, multi-select delete pill, lasso, Ctrl multi-select, Option draw-through, long-press 0.3 s | PORT |
| AN-17b | **[ledger]** Per-tool modifiers from the supplement: pencil and plain-marker Shift lock to horizontal or vertical after 5 pt; number placement ignores Shift; ⌘D assigns a new group ID only when duplicating several items; loupe magnification ranges differ by path (UI 1.1–6, source drag up to 12, decode 0.1–100) | PORT |
| AN-18 | Serialization: lenient decoding where only `tool` is required, ~38 fields, RTF text payload, cached PNGs, group IDs, seeds; saved-capture limits (16,384 px side, 128 Mi px, 128 MiB, RTF 4 MiB, coordinates ≤ 1e6, finite) | PORT (own format) |
| AN-19 | CaptureEditState (effects and beautify parameters) persisted per history revision, normalised on load | PORT |
| AN-20 | Colour palette: 12 presets, 7 custom hex slots filled by the sampler | PORT |
| AN-21 | Curated fonts (18) followed by all system families | BACKEND (per-platform curated list) |

### A.5 Capture, scroll capture and recording engine (CR)

| ID | Item | Tag |
| :--- | :--- | :--- |
| CR-01 | Freeze before focus changes so menus and tooltips are captured; optional cursor composite (`captureCursor` false) | BACKEND |
| CR-02 | Per-display capture, pointer display first; fallbacks on failure; window capture with real alpha corners and shadow for window-snap beautify | BACKEND (per-window capture: WL-HARD, available on Hyprland) |
| CR-03 | Preview downscale for overlay memory (max 1400 px preview image) | PORT |
| CR-04 | Scroll capture: accessibility permission gate (macOS), region-below-overlay frames, settle on two identical frames (30 tries, 10→80 ms backoff, last-frame fallback), scrollbar width scan, sticky header lock, vertical shift registration with ≥ H/10 rule and −1 px seam bias, incremental canvas, stop at 30,000 px (`scrollMaxHeight`), 8 misses or 6 zero shifts | PORT (algorithm) + BACKEND (frames) |
| CR-05 | Auto-scroll: pointer warp to the region centre, synthetic line-wheel bursts, speeds 1–4 (default 3); manual mode grabs every 150 ms while scrolling plus 250 ms settle | WL-HARD (virtual pointer on wlroots/Hyprland, XTest on X11, CGEvent on macOS; GNOME/KDE only via RemoteDesktop portal, which the host lacks) |
| CR-06 | Scroll HUD with strip count, size, Auto Scroll / Stop, live preview panel 200 pt wide beside the region | BACKEND |
| CR-07 | Recording lifecycle idle → preparing → recording ⇄ paused → stopping, session UUIDs, monotonic clock minus pauses | PORT |
| CR-08 | Session directory per take: movie, `cursor.mstl`, optional `camera.mp4`, `session.plist` status recording/complete/interrupted | PORT (own formats) |
| CR-09 | Normal mode bakes cursor and live overlays; editable mode hides the cursor, routes clicks and keys to telemetry, records the webcam separately, and re-bakes the webcam if its recorder fails | BACKEND (exclusion of own surfaces: WL-HARD) |
| CR-10 | Writer: H.264 High (Main for Low), no B-frames, 1 s keyframes, BT.709, fragmented MP4 every 10 s, bitrate = w·h·fps·bpp (0.12/0.22/0.40) × taper × codec factor within per-quality clamps | PORT (encoder: BACKEND) |
| CR-11 | Only complete frames accepted; 1 fps heartbeat after 1 s idle; mic added as the first audio track (mono AAC 128k) then system audio (stereo AAC 256k); owned audio copies; planar trimming; 10 s / 4096-buffer queue; 5 s pre-roll | PORT |
| CR-12 | Failure states: no frames in 10 s, audio overload, audio format change, append failure, 15 s finalisation watchdog, disk < 256 MB every 2 s, stop before sleep | PORT (sleep signal: BACKEND) |
| CR-13 | Audio merge dialog (380×160, two 0–1 sliders, Keep Separate / Merge Audio) with video stream copy and gains divided by max(1, Σ) | PORT |
| CR-14 | Streaming GIF: per-frame palettes, changed-rectangle deltas, disposal 1, held duplicates, 100 Hz delays | PORT |
| CR-15 | Cursor telemetry file: move/button/shape/shape-definition/key/start/pause/resume records, truncation tolerant; 120 Hz polling, stationary compression, shape images deduplicated | WL-HARD (position: image-copy cursor session or portal metadata; clicks/keys: macOS, X11, or Linux opt-in helper) |
| CR-16 | Cursor motion: critically damped spring ω = 34 − 27·s, sway −tanh(0.9·v)·0.24·a, press dip 0.82 and release overshoot, idle and typing fades | PORT |
| CR-17 | Live click highlight ring (systemYellow, 0.3 s, r 18 + 60·age) | WL-HARD (global clicks) |
| CR-18 | Live keystroke pill (bottom centre +40, black 0.65, r 14, 28 pt, 1.5 s hold, 0.6 s fade, shortcuts-only default) | WL-HARD (global keys) |
| CR-19 | Webcam bubble: level above the overlay, draggable, circle or rounded rect (r = size/5), 2 pt white 0.5 border, 80–480 pt (120), four corners at 12 pt | WL-HARD (camera: BACKEND; GNOME cannot place it or keep it on top) |
| CR-20 | Recording HUD 164×32 r 10: stop, pause/play, dot, "00:00" timer, drag handle; 8 pt above the region with flip; excluded from capture; `hideRecordingHUD` | WL-HARD (placement and on-top fail on GNOME; exclusion from capture: CR-23) |
| CR-21 | Menu bar stop button during recording | WL-HARD (SNI tray; GNOME has no tray without the AppIndicator extension, so the stop hotkey and HUD remain) |
| CR-22 | Captions by on-device speech recognition | BACKEND (Speech on macOS, whisper.cpp on Linux) |
| CR-23 | Self-exclusion of AriadShot's surfaces from recordings. MacShot excludes its HUD, selection outline and chrome through the ScreenCaptureKit exclusion list (`macshot/Capture/RecordingEngine.swift:247,316`). It bakes the webcam bubble, click ring and keystroke pill only in normal mode | WL-HARD (no Wayland capture protocol offers per-surface exclusion; per-desktop strategy in §5; DEV-22, DEV-23) |
| CR-24 | "When done" routes: Open editor / Show in Finder / Copy to clipboard (`recordingOnStop`, `macshot/UI/Windows/SettingsWindowController.swift:1787`) | PORT (Show in Finder: BACKEND through SH-34; copying a video on Wayland needs data-control or focus) |

### A.6 Studio video editor (VE)

| ID | Item | Tag |
| :--- | :--- | :--- |
| VE-01 | Procedural project model: read-only source; trim, crop (≥ 0.05), look (frame, cursor, zoom, keystrokes, camera, captions), zooms, censors, texts, cuts, speeds, freezes, captions; sorted-key JSON beside the take or in a hashed external folder | PORT (own format) |
| VE-02 | Frame look: enabled, background (gradient `linear-0` from the beautify catalogue, colour, image, wallpaper, blur σ = blur·short·0.045), aspect auto/16:9/9:16/1:1/4:3/3:4/4:5/16:10, padding 0.08 of the short side, radius 14 @1080p, shadow 0.6, hairline border | PORT; `.wallpaper` background: MAC-ONLY (substitute: it picks macOS system wallpapers by path, which cannot be shipped, so Linux reads the user's current wallpaper where readable and hides the option otherwise; DEV-19) |
| VE-03 | Cursor look: system/dot/ring, size 1.4, smoothing 0.5, idle and typing hide, click none/ripple/spotlight/ring, click colour, press bounce, motion blur 0.35, loop to start, sway | PORT |
| VE-04 | Zoom look: 1.8 (1.2–5), gentle 1.2 / smooth 0.85 / snappy 0.5 s, connect zooms within 1.2 s, motion blur 0.5, dead zone 0.45 | PORT |
| VE-05 | Keystroke, camera and caption looks with 9 anchor positions, sizes, mirror, shrink on zoom, 44 pt captions, max words 7 | PORT |
| VE-06 | Segments: zoom (1.2–5, fades 0.35, follow/automatic), censor (solid/pixelate 20/blur 30), text (6–400, fades 0.25), cut (merge), speed (0.25–10, varispeed audio), freeze (0.1–30, silence) | PORT |
| VE-07 | Auto-zoom planning from clicks and typing bursts (cluster 2.4 s, lead 0.7, tail 1.5, min 2.0, drop < 0.6, merge, first-event focus, spread fit, skip occupied) | PORT (needs click telemetry) |
| VE-08 | Renderer layer order: censors on source, crop and frame with shadow and border, camera transform with motion blur (1/40 s shutter, 3–10 samples), text overlays, pointer with sway/bounce/click effects, then captions, keystrokes, webcam | PORT |
| VE-09 | Window: dark palette tokens, 980×640 minimum, top bar 52, stage toolbar 44, inspector 56 + 304 with six sections, stage inset 28, transport 50, timeline 230 (ruler 26, clip lane 58 with thumbnails and waveform, lanes 30, gap 6) | PORT |
| VE-10 | **[ledger]** Keys: Space, ←/→ frame, Shift ±1 s, Home/End, Delete, Esc, I/O, Z/C/T add, K/L/J, =/−, ⌘Z/⌘⇧Z/⌘S/⌘⇧S/⌘C/⌘E; ignored while editing text | PORT |
| VE-11 | **[ledger]** Timeline interactions: scrub, trim (8 pt, 0.1 s gap), move, edge resize (6 pt, 0.2 s), 7 pt snapping to 0/duration/trim/playhead/edges, double-click add/edit, greedy overlay rows | PORT |
| VE-12 | Stage manipulation: 8 handles for crop/censor/text, aspect-locked 4-corner zoom rects, pinning moved automatic zooms, inline text editing | PORT |
| VE-13 | Undo of 200 full snapshots with gesture grouping; autosave after 1.2 s; synchronous save on quit | PORT |
| VE-14 | Export: render only when needed; high quality stream path vs transcoder; MP4 or GIF (5–30 fps, 15 default); scale ≤ 1; SRT with time remapping; Esc cancels; modeless progress | PORT (encoders: BACKEND) |
| VE-15 | Captions generate/regenerate/export/remove with the on-device privacy note | BACKEND |

### A.7 Image editor and secondary windows (ED)

| ID | Item | Tag |
| :--- | :--- | :--- |
| ED-01 | Detached editor window: "… Editor · HH:mm:ss", min 800×400, max 0.9 of the screen at open, chrome allowances 106×156, fit zoom ≤ 1, insets 84/50, zoom 0.1–8, centring clip view with hit forwarding | PORT |
| ED-02 | Top bar 32: size label, crop, flip H, flip V, add capture, Done (dirty only), zoom menu (⌘+, ⌘−, ⌘1 fit, 50/100 ⌘0/200) | PORT |
| ED-03 | Dirty tracking (never output, undo identity, edit state) and "Save changes?" sheet; ⌘Q closes the window | PORT |
| ED-04 | Editor outputs mirroring the overlay (copy + optional close, save/save as, quick save, pin, OCR, upload, share, remove background, Done commits to history) | PORT; Share: MAC-ONLY (substitute: no freedesktop share sheet exists, so Share is hidden on Linux; DEV-18) |
| ED-05 | Floating thumbnail: 240×160 × scale, r 12, 1.5 pt white 0.4, hover veil, four corner buttons, Copy/Save pills, body click dismisses, stacking per corner, 5 s auto-dismiss paused on hover, edge-drag and swipe dismiss, drag-out as a file in the chosen format | WL-HARD (layer-shell placement; GNOME cannot place it, keep it on top or show it on all workspaces; swipe dismiss only while the pointer is over it) |
| ED-06 | Shared image context menu: copy, save, save as, open in editor, pin, upload, Quick Look, OCR, rotate/flip transforms persisted to history, Open With, Share, Delete, Close All, Save All to Folder | PORT; Share, Quick Look and Open With: MAC-ONLY (substitute on Linux: Share hidden; Quick Look opens the pin-style preview; Open With calls the OpenURI portal with `ask`; DEV-18) |
| ED-07 | History overlay: backdrop plus 240 pt panel sliding in 0.12 s, tabs All/Screenshots/GIFs, trash with confirm, 200×160 cards with captions and hover hint, LRU 150 + 12 lookahead, keyboard navigation, drag to apps, dismiss on deactivation | WL-HARD (layer-shell placement; normal window on GNOME) |
| ED-08 | Pin window: ≤ 80% of the screen, shadow, movable, cursor-anchored zoom 0.1–5, top-right close/edit/zoom pill, reset on pill click, three-item menu, no opacity control | WL-HARD (always-on-top and self-move through layer-shell margins; no on-top guarantee on GNOME) |
| ED-09 | Pin from clipboard: images, RTF, RTFD, sanitised HTML, plain text rendered to a card (20,000 characters, spacing normalisation, area cap) | PORT (flavours: BACKEND) |
| ED-10 | OCR result window 720×460: preview column, translate header (30 languages), editable monospaced text with find, QR rows with Copy/Open Link, AI Search, Copy ⌘↩ | PORT |
| ED-11 | Upload toast: 380×56 top centre, slide 0.3/0.35 s, growth rules, 8 s / 6 s dismissal | WL-HARD (layer-shell placement; GNOME cannot place it top-centre above other windows) |
| ED-12 | Export progress (420×130, modeless, Esc cancel) and audio merge dialog | PORT |

### A.8 Services, storage and upload (SV)

| ID | Item | Tag |
| :--- | :--- | :--- |
| SV-01 | Beautify renderer: window/rounded modes, padding 48, radius 10, shadow 20, background radius 8, 18 mesh (3×3) + linear gradient styles, custom image with blur 0–50, synthetic title bar with traffic lights, calibrated ambient + contact shadows cast from the rounded alpha | PORT |
| SV-02 | Image effects: none, noir, mono, sepia 0.8, chrome, fade, instant, vivid; brightness, contrast, saturation, sharpness; legacy vivid migration | PORT (preset looks approximated) |
| SV-03 | Encoding: PNG default, JPEG, HEIC, WebP (≤ 16,383 px, straight alpha), AVIF; quality 0.85; 1× downscale; clipboard writes the chosen format + PNG (+ TIFF on macOS) with a generation guard | PORT (HEIC on Linux: optional) |
| SV-04 | Saving: folder vs ask, filename tokens and subfolders, sanitiser (200 bytes), atomic no-overwrite publish with "(n)" suffixes, export cancellation states, source leases with reader locks, per-item scratch folders | PORT |
| SV-05 | History: revision folders with image/thumb/preview/raw/annotations/edit files, atomic index, strict row validation, cleanup only of unindexed caches when the index is fully valid, count limit 10 or unlimited, order by last edit, bounded save queue (32 / 512 MiB), editable restore rules | PORT |
| SV-06 | Translation: Google endpoint (default), Apple Translation on macOS 15+, 30 languages, overlay generator | BACKEND |
| SV-07 | Uploads: imgbb (default provider), Google Drive (OAuth PKCE, idempotent upload, linear retry), S3-compatible SigV4 (R2, MinIO, B2); streamed bodies; progress; offline build removes them | BACKEND (OAuth flow differs per OS) |
| SV-08 | Layout-independent shortcut matching for non-Latin layouts | BACKEND |

### A.9 Settings (ST)

| ID | Item | Tag |
| :--- | :--- | :--- |
| ST-01 | Settings window 620×520, fixed size, seven icon tabs (General, Capture, Shortcuts, Tools, Recording, Uploads, About), 180 pt label column, section headers, toggle grids, footer credit link | PORT |
| ST-02 | **[ledger]** Every control, conditional state and default listed in the catalogue and its supplement (unset key vs effective default modelled separately). Ledger lines carry their tab and domain: Recording-tab lines are `rec`, update lines are `release` | PORT |
| ST-03 | Hotkey, chord and single-key recorders with validation, clear and reset | PORT (registration and read-back per backend: §6.2) |
| ST-04 | Live filename preview with a fixed sample date | PORT |
| ST-05 | Menu order editor, theme presets with colour wells, settings backup with relaunch prompt | PORT |
| ST-06 | Launch at login | BACKEND (XDG autostart / login item) |
| ST-07 | Translation engine section only where Apple Translation exists | MAC-ONLY (Linux shows its own engines; DEV-37) |
