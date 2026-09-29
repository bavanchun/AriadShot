<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 14. Gates and Milestones

Part of the [AriadShot technical specification](README.md). This file defines what each milestone must deliver and
prove, the M0 go/no-go gates, the platform feature gates, and the initial deviation register. Decisions:
[ADR 0001](../adr/0001-architecture-and-stack.md), [ADR 0002](../adr/0002-foundation-and-workflow.md). Every gate
outcome is recorded as its own ADR in `docs/adr/`.

## 1. Milestone model

**Artifacts.** Every milestone ends with four artifacts, generated from `parity/` and committed as a section of the
release notes:

1. the parity-ledger report (verified, deviation or open, per platform);
2. the performance budgets that apply to the milestone, with measured numbers;
3. the robustness gates ([12 §11](12-testing-strategy.md));
4. the updated deviation register.

**Domains.** Milestones gate on domains, not on row prefixes, because several rows under a screenshot prefix hold
recording or studio items. Default rule: SH, OV, TB, AN, ED, SV and ST rows are `shot`; CR rows are `rec`; VE rows are
`studio`; `release` covers updates, the offline build and packaging. Ledger lines override the default where their
content says so:

| Ledger lines | Domain | Why |
| :--- | :--- | :--- |
| CR-01, CR-02, CR-03 (freeze before focus change, per-display capture, preview downscale) | `shot` | screenshot prerequisites, needed in M1 |
| CR-04, CR-05, CR-06 (scroll capture) | `shot` | scroll capture is a screenshot feature (M2) |
| CR-14 (streaming GIF writer), CR-22 (captions) | `studio` | M3 has no GIF recording because MacShot exports GIF only from the studio (VE-14, M4; every caller of the GIF writer is in the studio editor, for example `macshot/UI/Editor/Video/VideoEditorExporter.swift:122` and `macshot/UI/Editor/VideoEditorWindowController+Export.swift:86`), and captions are generated in the studio |
| OV-30; the recording-popover lines of OV-31; TB-09; the Keystrokes, Mic and Webcam menus of TB-15 | `rec` | recording setup UI |
| ED-12 audio-merge lines | `rec` | |
| ED-12 export-progress lines | `studio` | |
| SH-04 `record`, `record-fullscreen`, `stop-recording`; SH-08 Record Area, Record Screen, Show Recordings; SH-10 Record Area and Record Screen slots; the Show Recordings line of SH-34 | `rec` | |
| SH-34 reveal lines for ED-06 and history items | `shot` | they act on screenshots (M2) |
| SH-04 `open?file=` for videos; SH-08 Open Video…; the SH-30 video route; SV-07 video uploads | `studio` | |
| ST-02 Recording-tab lines, except its Scroll Capture section (`shot`) | `rec` | |
| SH-25, SH-26; ST-02 update and beta-channel lines; packaging and signing lines | `release` | |

**Parity bars per platform.** Parity = verified ledger lines ÷ applicable lines ([12 §5](12-testing-strategy.md)).
MacShot parity is always measured against commit `b4d4f3a`.

| Milestone | Hyprland (reference) | macOS (contingent on G7) | KDE Plasma 6 / X11 | GNOME |
| :--- | :--- | :--- | :--- | :--- |
| M1 MVP | 100 % of the M1 atomic set (§4.1) | builds, launches, captures, copies; no parity claim | best effort | best effort |
| M2 Screenshot parity | 100 % of `shot` lines; every `WL-HARD` shot line verified or registered | 100 % of the M1 atomic set with the macOS backends of §4.2 | 100 % of the M1 atomic set, measured in nested KWin and Xvfb/Xephyr | no claim |
| M3 Recording parity | + 100 % of `rec` lines | 100 % of `shot` lines | ≥ 90 % of `shot` lines | no claim |
| M4 Studio parity | + 100 % of `studio` lines | + `rec` lines | + `rec` lines where the backend allows | no claim |
| M5 Release 1.0 | 100 % of applicable lines, including `release` | 100 % of applicable lines | ≥ 95 % of applicable lines | every line not verified in a GNOME test environment is in the deviation register |

**Definition of done** per line, slice and milestone: [12 §5.4](12-testing-strategy.md).

## 2. M0 gates

M0 proves or kills the load-bearing assumptions before feature work. It produces measurements and decisions, not
parity.

### 2.1 Lanes

| Lane | Work, in order | Needs from the owner |
| :--- | :--- | :--- |
| A. Harness (Linux, no live session) | test support library (exact and CIEDE2000 comparator, deterministic font registration); parity-ledger schema, generator, hand-written lines and import of the deviation register, published in `parity/` before any M1 slice that needs them starts; **G3** with the bundled fonts | nothing |
| B. Wayland | headless Sway harness with protocol inventory (tier 2); nested Hyprland harness with a second headless output inside the nested instance (tier 3); **G1**; **G2** (synthetic scene, then live test); **G4** | install `layer-shell-qt`, `sway`, `wayland-utils`; approve starting a nested Hyprland window; approve each live capture test; connect the P1 second display for the final G1, G2 and G8 measurement runs (agents ask and wait) |
| C. Media | **G5**; then **G6** PipeWire path; G6 image-copy path after lane B's G2 | install `vulkan-headers`, `vulkan-validation-layers`, `gpac`; approve live recording trials |
| D. macOS (the owner's Mac) | **G7**, then the MacShot reference corpus for the M1 atomic set | Mac model, OS and VM capacity; running the Mac steps |
| — | **G8** baselines on P1–P3, closing M0 | live-session approval |

Lanes A and D can start as soon as the scaffold merges; lanes B and C start once their packages are installed.
Development iterations of G1 and G2 run on nested Hyprland; the final measurement runs use the live session.

**Gate code policy.** Gate spikes that prove a design become the first real code of their module and are reviewed
like any pull request. Throwaway experiment code is not merged; its measurements go into the gate's ADR.

**Decision checkpoint.** At the end of M0 each gate outcome is written as an ADR: the overlay host chosen, the
recording source chosen, and every performance budget kept, revised or rejected with its numbers. Only the three
triggers in [01 §11](01-architecture-overview.md) can reopen the stack decision.

### 2.2 Gate table

| Gate | Proves | Pass criteria | If it fails |
| :--- | :--- | :--- | :--- |
| G1 Overlay host | Qt surfaces work as overlays on Hyprland layer-shell | steps 1–7 of §2.3 for both H1 and H2, on P1 and on P2 | the outcomes of §2.4 |
| G2 Capture | the capture ordering contract ([04 §4](04-capture-and-overlay.md#4-capture-ordering-contract)), not just advertised globals | first a synthetic scene in a nested compositor, then a live test: a per-output source; no AriadShot pixels in 100 captures taken while a thumbnail and a pin are visible; the rotated output upright; buffer renegotiation; `failed` falling back to screencopy; the cursor matching `captureCursor`; W1 latency measured | screencopy becomes primary; if both fail, a ScreenCast snapshot path is used and the latency budget is revised with numbers |
| G3 Still rendering contract | [03 §2](03-rendering-contracts.md#2-still-image-contract) | three representative tools, a mesh gradient, the calibrated shadow, and text with emoji; PNG export equals the canonical image; presentation exact at integer scale; lossy thresholds set | fix the inputs (fonts, device pixel ratio); if the failure is specific to `QPainter`, reopen the stack decision |
| G4 Wayland connections | the two-connection model ([04 §3](04-capture-and-overlay.md#3-wayland-connection-ownership)) | 10 minutes of repeated captures plus unfocused clipboard copies: no stall longer than one frame; a data-control copy survives overlay dismissal | bind on Qt's connection with a shared queue, behind a Qt-version-pinned adapter |
| G5 Studio contract | [03 §3](03-rendering-contracts.md#3-studio-contract) | a decoded FFmpeg frame drawn through `QRhi` (Vulkan set explicitly) in a `QRhiWidget` and offscreen, then encoded with VA-API into fragmented MP4; preview matches pre-encode within ΔE00 ≤ 1; decoded export meets the thresholds; the OpenGL fallback runs; `Qt6::GuiPrivate` links on the Qt 6.8 floor | fix; if the failure is specific to `QRhi` on both APIs and an equivalent wgpu spike passes, reopen the stack decision |
| G6 Recording path | [05 §3](05-recording-and-studio.md#3-sources-buffer-paths-and-cadence) and the writer states | W4 on image-copy DMA-BUF and on PipeWire: dropped frames, CPU use and copy count recorded; 200 kill trials | choose the better path; if neither meets W4, revise the budget with the measurements, never silently |
| G7 macOS environment | the contingency on every macOS claim, at both ends of the supported range | §2.5 | §2.5 |
| G8 Baselines | the performance budgets ([12 §9](12-testing-strategy.md)) | the benchmark harness records a baseline for every budget on P1–P3 | each budget kept, revised or rejected in an ADR with numbers |

### 2.3 G1 acceptance steps

G1 runs on topology P1 (both outputs, including the rotated one) and in a nested Hyprland at fractional scale 1.25
(P2). H1 (`QWidget` + `layer-shell-qt`) and H2 (`QQuickWindow` + `layer-shell-qt`) are both built on the same renderer
and chrome and both run all seven steps.

1. **Mapping.** One OVERLAY-layer surface per output, anchored to all edges with exclusive zone −1 over the bar; correct
   size and orientation on the rotated output; correct buffer scale at 1.25.
2. **Keyboard.** Exactly one surface requests exclusive keyboard at a time: the one holding the selection, otherwise the
   one on the pointer's output ([04 §5](04-capture-and-overlay.md#5-surface-hosts)). Moving the pointer to the other
   output while no selection exists hands the keyboard over without losing keys; while a selection exists the keyboard
   stays with it, so keys act on the selection wherever the pointer is. Esc works from either output.
3. **Popovers.** Chrome popovers, tooltips and the Save right-click menu open, follow the pointer and dismiss correctly
   inside the surface. A Qt menu opened from the surface is also tried, as an optional check.
4. **IME** in the canvas text control: Vietnamese Telex (fcitx5 Unikey or Bamboo) with inline preedit and commit, first
   through the `QT_IM_MODULE=fcitx` path, then through `text-input-v3`; a CJK engine whose candidate window appears
   within 50 px of the caret.
5. **Re-show latency.** Hotkey to first interactive frame on the pointer output ≤ 150 ms p95, in both the
   dormant-unmapped and dormant-mapped variants.
6. **Frame time.** Drag frame time ≤ 16.7 ms p95 on P1 and on P3.
7. **Cursors.** Cursor shapes through `wp_cursor_shape_v1`, and the hidden cursor under the pencil dot.

Step classes fix the decision rule. **Hard** (1, 2): a host that fails either is ineligible for overlay-class
surfaces. **Text** (4): an eligible host that fails it may be chosen only with the DEV-21 fallback, and only if no other
eligible host passes it. **Fixable** (3, 7): must pass on the chosen host before M1; the fixes live in the chrome layer
or the host adapter. **Budget** (5, 6): measured; P1 results decide.

**Decision rule:** H1 if it passes all seven steps; otherwise H2 if H2 passes all seven; otherwise the outcomes of
§2.4 in order. When two eligible hosts differ, prefer the one that passes step 4, then the better P1 results on steps
5 and 6, then H1.

### 2.4 G1 outcomes

| # | H1 | H2 | Decision | Acceptance condition | Cost |
| :-: | :--- | :--- | :--- | :--- | :--- |
| O1 | passes 1–7 | any | H1 | H1 passes 1–7 on P1 and P2 | none |
| O2 | fails only 1 | passes 1–7 | H2 for every overlay-class surface; ordinary windows stay Widgets | H2 passes 1–7 on P1 and P2 | 1–2 weeks |
| O3 | fails only 2 | passes 1–7 | H2 | as O2 | 1–2 weeks |
| O4 | fails only 4 | passes 1–7 | H2 (keeps in-place text entry, avoids DEV-21) | as O2, with Vietnamese Telex passing on the fcitx path | 1–2 weeks |
| O5 | fails only 7 | passes 1–7 | H2 (one adapter swap instead of an open-ended cursor fix) | as O2 | 1–2 weeks |
| O6 | fails any of 3, 5, 6 | passes 1–7 | H2 | as O2 | 1–2 weeks |
| O7 | passes 1, 2, 4, 7; fails 3 | same | H1; every menu the overlay opens becomes a chrome view inside the surface | step 3 passes on H1 with chrome-drawn menus and popovers | up to 1 week |
| O8 | passes 1, 2; fails 5 | passes 1, 2; fails 5 | the host with the lower P1 p95 that passes 4, if ≤ 250 ms; W1 revised to the measured p95. If neither meets 250 ms, build H3 and re-measure; if H3 also misses, keep the faster Qt host with W1 set to its p95, capped at MacShot's under-400 ms case | the chosen host's p95 recorded and ≤ 400 ms; dormant-mapped variant measured for both hosts | none, or 4–6 weeks for H3 |
| O9 | passes 1, 2; fails 6 | passes 1, 2; fails 6 | the host with the lower P1 frame time that passes 4; rendering mitigations first (per-layer tile caches, damage clipping, presentation downscale for large outputs); export pixels unchanged | P1 ≤ 16.7 ms p95 is a hard M1 condition; a P3 miss after mitigations becomes a documented 4K budget revision, re-gated at M5 | up to 2 weeks |
| O10 | passes 1, 2; fails a mix of 3, 5, 6 | passes 1, 2; fails a different or overlapping mix | apply O7, O8 and O9 per failed step; host by the precedence above | each failed step meets its row's condition on the chosen host | sum of the rows |
| O11 | passes 1, 2, 3; fails 7 | same | H1; the pointer is hidden over the surface and the canvas draws the cursor (as MacShot does for the pencil dot), with shapes from the cursor theme | step 7 passes on H1 with drawn resize, crosshair and open-hand cursors | up to 1 week |
| O12 | fails 4 | fails 4 (both otherwise eligible) | the host chosen by the other rows, plus the IME fallback: text entry in a pop-up parented to the layer surface, then the editor window (DEV-21) | Vietnamese Telex and one CJK engine commit correctly through the fallback; Enter, click-outside, Esc and scoped undo unchanged | 1–2 weeks |
| O13 | fails 1 or 2 | fails 1 or 2 | H3, the hand-written layer-shell client; overlay chrome loses Qt accessibility; text entry uses `text-input-v3` directly (O12 applies if it fails) | H3 passes 1, 2, 3, 5, 6, 7 on P1 and P2; step 4 passes or O12 applies | 4–6 weeks |
| O14 | fails 1–3 | fails 1–3 | H3 also fails 1–3: the compositor gives no client an interactive overlay. Fall back to a fullscreen toplevel with Hyprland window rules and disclose it | the fallback maps on the pointer's output, takes the keyboard and covers the bar under the documented rules | about 1 week |

### 2.5 G7 — macOS environment

**Environments.** Two tested OS versions: the owner's physical Apple Silicon Mac on macOS 26, and macOS 14 as a VM on
that Mac (Apple's Virtualization framework), registered as a self-hosted runner for this repository only (labels
`self-hosted, macos-14, owner-mac`; `workflow_dispatch` and schedule on `main` only; environment `owner-mac`; reverted
to a clean snapshot after each run). Until the VM exists, the hosted `macos-14` job is the only macOS 14 runtime check.

**Pass criteria, on each version:**

1. CI builds with the current SDK and deployment target 14.0, and the test suite passes.
2. MacShot builds from `b4d4f3a` (the corpus is captured on macOS 26 only).
3. The overlay skeleton works: ScreenCaptureKit capture, a non-activating `NSPanel`, IME inside the panel.
4. The pinned Qt minor lists macOS 14 as supported.
5. **The un-notarized release path** (no notarization dry run; there is no Developer ID): an inside-out ad-hoc-signed
   bundle passes `codesign --verify --deep --strict`; after a quarantined download (served from a local server or a
   draft artifact, never published) the first open is blocked and the documented steps open it (Open Anyway on macOS
   26, Control-click → Open on macOS 14).
6. **Permission and Keychain experiment**: grant Screen Recording to build *n*, replace it in `/Applications` by build
   *n+1*, and record whether the grant still matches and whether AriadShot's Keychain items stay readable without a
   prompt ([11 §4](11-security-privacy.md#4-secrets)), once with ad-hoc signing and once with a self-signed project
   certificate; the ADR records the macOS builds observed. The dimensions are macOS version × signing mode. The update
   path is manual replacement only, because Sparkle does not exist until M5, where macOS feature gate 7 repeats the
   measurement through the Sparkle installer. The result decides whether the owner keeps ad-hoc signing or signs
   releases with the self-signed certificate ([13 §7](13-build-ci-release.md#7-macos-distribution)).

**If G7 fails.** Without a Mac, macOS stays "builds only" and the corpus falls back to MacShot's published assets with
reduced confidence. If the macOS 14 environment cannot be set up or fails the skeleton, the supported minimum narrows
to the oldest version that passes and DEV-27 is updated. macOS milestones slip; Linux milestones do not.

## 3. Platform feature gates

### 3.1 macOS

Each gate must pass on both G7 environments before its ledger lines count; gate 7 runs once on the Mac and is then
verified by launching the signed build in the VM.

1. **Panel.** A non-activating `NSPanel` at level 257 over full-screen applications on every Space; it becomes key
   without activating AriadShot, Esc works, and the previous application keeps its active state.
2. **IME.** Vietnamese and CJK input sources in the canvas text control inside a non-activating panel, through Qt's
   `NSTextInputClient`.
3. **Capture and onboarding.** ScreenCaptureKit capture, pointer display first; the Screen Recording permission prompt
   and the onboarding window (SH-22) with relaunch-free detection; window capture with real alpha.
4. **Shell integration.** Status item and menu, Carbon hotkeys with MacShot's default chords, the standard main menu
   (SH-31), the URL scheme, launch at login through `SMAppService`.
5. **Recording.** ScreenCaptureKit system audio excluding AriadShot's own audio, the AVCapture microphone, the
   exclusion list for the HUD, and `IOSurface` → `QRhi` Metal texture import for the studio.
6. **Permissions.** Microphone, Camera, Accessibility (auto-scroll), Input Monitoring (keystrokes) and Speech, each with
   MacShot's usage strings rewritten for AriadShot.
7. **Distribution.** Inside-out ad-hoc signing of every Qt and FFmpeg framework and the Sparkle XPC services passes
   `codesign --verify --deep --strict`; the Gatekeeper first-open steps work; a Sparkle EdDSA update installs and is
   released from quarantine; and the privacy-grant and Keychain behaviour across that Sparkle update is measured (the
   update-path dimension deferred from G7). No notarization and no Developer ID (DEV-40).

### 3.2 GNOME

In a GNOME 48+ VM, before any GNOME ledger line counts:

1. Whether the Screenshot portal shows a dialog with `interactive=false`, whether it returns the whole desktop, and how
   long it takes.
2. Fullscreen overlay placement per output, focus, panel coverage and workspace behaviour.
3. The GlobalShortcuts portal.
4. Clipboard ownership before focus loss.
5. ScreenCast with `multiple: true`, restore tokens and invalidation on hotplug.

## 4. Milestones M1 to M5

### 4.1 M1 — MVP: daily-driver screenshot tool on Hyprland

**Goal.** A Hyprland user can replace their desktop's screenshot flow with AriadShot. **Parity meaning.** Every line
in the M1 atomic set behaves exactly as MacShot does, verified by the tests of [12](12-testing-strategy.md). Everything
else is absent: it does not appear in menus, toolbars, Settings or the Shortcuts tab. A MacShot action outside M1 that
can still be requested through a URL or the CLI shows an error pill ("“<action>” isn't available in this version of
AriadShot") and returns a CLI error with the same text; it is never ignored silently.

**M1 atomic set** (Hyprland, English only; "Later" names what is deferred):

- **Shell.** SH-01; SH-02 without the updater; SH-03 as mapped to capture states
  ([08 §4](08-platform-integration.md)); SH-07, SH-09, SH-12 to SH-18; SH-21, SH-24, SH-28; SH-31 (Linux chords); SH-32;
  ST-06 (launch at login through XDG autostart). SH-30 image route only: Open Image…, `open?file=` for images, and the
  desktop entry's `MimeType=` for MacShot's eight image types, with GIF opening in the image editor.
- **SH-04 URL and CLI actions.** `capture`, `capture-fullscreen`, `capture-last`, `quick-capture`, `settings`,
  `history`, `open?file=` for images, `edit?id=`. Later: `ocr`, `ocr-translate`, `scroll-capture` (M2); `record`,
  `record-fullscreen`, `stop-recording` (M3).
- **SH-08 status menu.** Capture Area, Capture Screen, Quick Capture, Capture Last Area, Capture Delay (None, 3, 5, 10,
  30 s), Recent Captures, Show History Panel, Open Image…, Open from Clipboard, Pin from Clipboard, Settings…, Quit.
  Later: Capture OCR & QR and Scroll Capture (M2); Record Area, Record Screen, Show Recordings (M3); Open Video… (M4);
  Check for Updates… (M5).
- **SH-10 hotkey slots registered.** Capture Area, Capture Screen, History, Quick Capture, Capture Last Area, Open from
  Clipboard, Pin from Clipboard, Clear History, with the Linux defaults of [08 §3](08-platform-integration.md). Later:
  Capture OCR and Scroll Capture (M2); Record Area and Record Screen (M3).
- **SH-11 single keys.** Pencil `p`, arrow `a`, line `l`, rectangle `r`, ellipse `o`, marker `m`, text `t`, number `n`,
  censor `b`, highlight `h`, colour sampler `i`, stamp `g`; measure and loupe (unbound by default); move selection
  Space, adjust selection `s`, open in editor `e`, pin `f`; copy, save, invert. Later (M2): upload `u`, OCR, translate,
  scroll capture, beautify, remove background.
- **Overlay.** OV-01 to OV-08; OV-10 to OV-24, including OV-18 ratio presets and OV-23 cross-display stitching; OV-27;
  OV-28 limited to confirm, quick save, save, save as and pin; OV-29, OV-32, OV-33, OV-34, OV-36. Later: OV-25, OV-26,
  OV-35, the non-recording OV-31 popovers, and the OCR, upload and Share-substitute flows of OV-28 (M2); OV-30 and the
  recording popover (M3).
- **Toolbars.** TB-01 to TB-06, TB-10, TB-11, TB-13. TB-07 buttons: pencil, line, arrow, rectangle, ellipse, marker,
  text, number, censor, highlight, loupe, stamp, colour sampler, measure, colour, undo, redo, invert; later (M2)
  adjust, beautify, remove background. TB-08 buttons: cancel, move, open in editor, copy, save, pin; later the Share
  substitute, upload, OCR, translate and scroll capture (M2) and record (M3). TB-12 rows for the M1 tools only (the
  marker without its Smart toggle, the censor without Text Only and Auto). TB-14 colour, font and emoji popovers;
  TB-15 Save menu (Save As…, Save to <folder>).
- **Annotations.** AN-01 to AN-05, AN-06a, AN-07, AN-08, AN-09a, AN-11 to AN-14, AN-16, AN-17, AN-17b, AN-18, AN-19
  (with default effects and beautify state), AN-20, AN-21. Later (M2): AN-06b, AN-09b, AN-10, AN-15.
- **Capture engine.** CR-01; CR-02 per display (window capture with alpha arrives with beautify at M2); CR-03 (MacShot
  defines the 1400 px display preview but calls it only from tests, so its ledger line verifies the helper's rule and
  AriadShot applies it only as a presentation mitigation, [03](03-rendering-contracts.md)).
- **Editor and windows.** ED-01 to ED-04 with outputs copy, save, save as, quick save, pin and Done; ED-05; ED-06 items
  copy, save, save as, open in editor, pin, rotate and flip transforms, delete, close all, save all to folder (later,
  M2: upload, OCR, the Quick Look substitute, Open With); ED-07, ED-08; ED-09 for image flavours (later, M2: text, RTF
  and HTML cards).
- **Services.** SV-03 (PNG and JPEG), SV-04, SV-05, SV-08.
- **Settings.** ST-01 with the General, Capture, Shortcuts and Tools tabs, showing only lines whose features exist in
  M1; ST-03, ST-04; ST-05 (menu-order editor and theme presets; backup at M2); TB-02 theme presets.
- **Infrastructure.** The daemon, the tray, GlobalShortcuts portal registration with the Hyprland protocol as
  fallback, the CLI and URL scheme, clipboard through data-control, and the AUR `-git` package.

**M1 slice order.** Work is delivered in ledger-driven vertical slices, each end to end with the tests that prove it:

| # | Slice | Main rows |
| :-: | :--- | :--- |
| 0 | Walking skeleton: CLI or hotkey → capture pointer output → overlay shows the frozen frame → drag → Copy → clipboard | SH-13, SH-17 (confirm), CR-01 to CR-03, OV-01, OV-15, OV-16 (partial) |
| 1 | Settings registry and store, defaults-parity test | ST registry, SV-04 atomic publish, SH-24 |
| 2 | Selection behaviour | OV-02 to OV-08, OV-10 to OV-24, OV-36 |
| 3 | Toolbars and options row | TB-01 to TB-07, TB-10, TB-11, TB-13 |
| 4 | Annotation model and geometry module | AN-01, AN-02, AN-17, AN-17b, AN-18 |
| 5 | Tools, in families | AN-03 to AN-05, AN-06a, AN-07, AN-08, AN-09a, AN-11 to AN-14, AN-16, AN-19 to AN-21, TB-12, TB-14, TB-15 |
| 6 | Outputs and history | SV-03, SV-05, OV-27 to OV-29, OV-32 to OV-34, ED-06 subset |
| 7 | Editor, thumbnail, pin, history panel | ED-01 to ED-05, ED-07 to ED-09 subsets |
| 8 | Shell | SH-01 to SH-03, SH-07 to SH-12, SH-14 to SH-16, SH-18, SH-21, SH-28, SH-30 (images), SH-31, SH-32, ST-06 |
| 9 | Settings window (M1 tabs) | ST-01, ST-03 to ST-05, TB-02 |
| 10 | M1 exit | AUR `-git`, parity report, budgets, robustness gates, deviation register |

Inside a slice, work follows the module graph: `core` → `render` → `ui` → host and window wiring → backends. Tests are
written first from the ledger's MacShot values. Parallel slices need disjoint directory-level write sets.

### 4.2 M2 — Screenshot parity

**Goal:** every `shot` line. **Parity meaning:** Hyprland 100 % of `shot` lines with every `WL-HARD` shot line verified
or registered; macOS the M1 atomic set; KDE and X11 the M1 atomic set in their CI environments.

M2 adds:

- **Image features:** beautify (all gradient styles, window and rounded modes, window-snap alpha capture), effects, all
  image formats.
- **Recognition:** OCR and QR with the OCR window, translation and the translate overlay, the smart marker (AN-06b),
  text-only censoring (AN-09b), auto-redaction (PII, faces, people), background removal with the model download flow
  (SH-33).
- **Capture:** scroll capture with auto-scroll (CR-04 to CR-06, OV-35).
- **Sharing:** the Share, Quick Look and Open With substitutes; reveal in file manager from ED-06 and history; pin from
  clipboard text cards; open from clipboard; uploads (imgbb, S3, and Drive once AriadShot's OAuth client exists) with
  the upload toast.
- **App-wide:** the complete Settings window with backup and import (SH-23); the 40 locales with runtime switching and
  right-to-left mirroring (SH-27); the Desktop Integration page; the remaining `shot` lines of SH-04, SH-08, SH-10 and
  SH-11.

**macOS content of the M2 gate** — the M1 atomic set with each row's macOS equivalent: Carbon hotkeys with MacShot's
defaults; the pasteboard (PNG and TIFF, plus the chosen format when `clipboardIncludesImageFormat` is on); the status item with the 22 pt template icon; `NSPanel` overlays
at levels 257/258, non-activating, on all Spaces; ScreenCaptureKit sequential capture, pointer display first;
CGWindowList window snapping; SF Symbols, the system font and Apple Color Emoji; the standard main menu (SH-31); the
`ariadshot://` scheme and document types (SH-30); launch at login through `SMAppService`; the Screen Recording
onboarding window (SH-22); an ad-hoc-signed, never-notarized build with the documented Gatekeeper first-open steps
(DEV-40), or the self-signed project certificate if the owner chose it after G7. macOS feature gates 1–4 must pass.

### 4.3 M3 — Recording parity

**Parity meaning:** Hyprland adds 100 % of `rec` lines, with click and keystroke telemetry through the opt-in helper;
macOS reaches 100 % of `shot` lines; KDE and X11 reach at least 90 % of `shot` lines.

M3 adds region and full-screen recording with the countdown; the recording setup bar and popover (TB-09, OV-30,
OV-31); the HUD, click ring, keystroke pill and webcam bubble; microphone and system audio as separate tracks and the
audio-merge dialog; fragmented MP4 with the writer states and crash recovery; telemetry capture and the
tray stop button; the "When done" routes (CR-24) and the Show Recordings line of SH-34; the self-exclusion strategy,
including the `no_screen_share` test (CR-23); the opt-in input helper for click and keystroke telemetry on Wayland
(DEV-24), with its ADR. There is no GIF *recording*: MacShot records only video and exports GIF from the studio (VE-14,
M4), so a GIF recorder would be invented behaviour.

### 4.4 M4 — Studio parity

**Parity meaning:** Hyprland adds 100 % of `studio` lines; macOS adds the `rec` lines (macOS feature gate 5).

M4 adds the project model and autosave, the inspector with its six sections and stage manipulation; the timeline with
all lanes and snapping, auto-zoom and cursor restyling from telemetry; captions with on-device transcription and SRT
export (CR-22); MP4 export and GIF export through the streaming GIF writer (CR-14), both under the studio contract;
copy and upload of videos; the export progress window;
the video open route (SH-30 video, Open Video…) with the no-telemetry mode.

### 4.5 M5 — Release 1.0

**Parity meaning:** Hyprland and macOS 100 % of applicable lines, including `release`; KDE Plasma 6 and X11 at least
95 %; GNOME: every line not verified by its VM gates registered as a deviation.

M5 adds the KDE, GNOME, X11 and generic wlroots backends up to their tier promises; AppImage and Flatpak packaging; the
ad-hoc-signed, un-notarized macOS DMG with Sparkle EdDSA updates (macOS feature gates 6–7; DEV-40); updates (SH-26)
and the offline build
(SH-25); the macOS catch-up to full parity; accessibility; performance tuning to every kept budget; the licence release
gates ([13 §8](13-build-ci-release.md#8-licensing-and-provenance)); documentation; and a review of upstream MacShot
changes since the pinned commit. An optional GNOME Shell extension is evaluated here, not before.

## 5. Initial deviation register

Parity is always reported against the deviation register. This table seeds it; in M0 the harness lane imports it into
`parity/deviations.jsonl`, which then becomes the only authoritative copy, and this section is replaced by a pointer.
The register grows with every decision that changes user-visible behaviour. Platform letters are defined in the
[README](README.md#conventions).

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
| DEV-40 | Developer-ID-signed, notarized and stapled DMG (upstream `.github/workflows/build-release.yml:62-93, 291-394`); Homebrew cask `macshot` (upstream `README.md:40-43`); a stable signing identity, so privacy grants and Keychain access carry across updates | ad-hoc-signed, never-notarized DMG; Gatekeeper first-open steps; no official Homebrew cask (optional own tap, owner-approved); Sparkle EdDSA-only updates; whether Screen Recording, Accessibility and Keychain access survive an update is measured in G7 and at macOS feature gate 7, and a self-signed project certificate is the owner's option if they do not | no paid Apple Developer ID, by owner decision ([ADR 0002](../adr/0002-foundation-and-workflow.md)) | M |
| DEV-41 | Manual scroll capture reacts to global scroll-wheel events: a capture every 0.15 s while scrolling, settled 0.25 s after scrolling stops (`macshot/Capture/ScrollCaptureController.swift:100,104`) | the same cadence and settle rule, driven by frame changes of the captured region | other clients' wheel events are not delivered on Wayland | W |
| DEV-42 | High-quality studio export uses `AVAssetExportPresetHighestQuality`, whose bitrate Apple chooses (`macshot/UI/Editor/Video/VideoEditorExporter.swift:98`) | MacShot's own encoding plan at quality High, through FFmpeg, on both platforms | one encoder implementation on both platforms ([ADR 0001](../adr/0001-architecture-and-stack.md)) | A |
| DEV-43 | The keystroke pill and keystroke looks show ⌃ ⌥ ⇧ ⌘; the shortcuts-only filter keys on ⌘, ⌃ and ⌥ (`macshot/Capture/KeystrokeTimeline.swift:40,47,78-81`) | Linux key names (Super, Alt, Ctrl, Shift); the filter keys on Super, Ctrl and Alt | Linux keyboard conventions | L |
| DEV-44 | The 18 mesh gradient styles exist only on macOS 15+ and come first in the list, so on macOS 14 the list has 30 styles and indices shift (`macshot/Services/BeautifyRenderer.swift:96-98`) | all 48 styles on every platform, one index space | mesh gradients are drawn by AriadShot's own renderer ([03](03-rendering-contracts.md)) | M |
| DEV-45 | PII patterns (`\d` in `NSRegularExpression`) and the planner's digit test (`CharacterSet.decimalDigits`) accept any Unicode decimal digit (`macshot/Services/AutoRedactor.swift:27-40`, `macshot/Services/PIIRedactionPlanner.swift:143`) | ASCII digits only, on every platform | identical results on both platforms from one regex engine | A |
| DEV-46 | The `open?file=` URL action always opens the image editor, so a video path fails silently (`macshot/AppDelegate.swift:2396-2425, 2457`) | the SH-30 file router applies: videos open in the studio, images in the image editor | one open route for every entry point | A |
| DEV-47 | SH-29: a one-line SIGTERM diagnostic written with async-signal-safe calls to `~/Library/Logs/macshot/termination.log`, to tell memory-pressure kills from normal quits (`macshot/AppDelegate.swift:89-112, 654-664`) | macOS keeps it (as `AriadShot/termination.log`). On Linux SIGTERM is a normal clean quit; fatal signals append one line (signal, version, UTC time) and raw return addresses to `$XDG_STATE_HOME/ariadshot/logs/crash.log` (0600), then re-raise; no memory, registers or user data | no Jetsam on Linux; SIGTERM is the normal session-end signal | L |
