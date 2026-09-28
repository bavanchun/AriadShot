# AriadShot — Product Requirements Document

**Status:** draft for maintainer review · **Version:** 0.1 · **Date:** 2026-09-28 · **Parity baseline:** MacShot,
<https://github.com/sw33tLie/macshot>, commit `b4d4f3a` (2026-09-27).

| Related document | Role |
| :--- | :--- |
| `docs/adr/0001-architecture-and-stack.md` | Binding architecture and stack decision. Holds the MacShot parity digest (row IDs such as `SH-01`), the deviation register (`DEV-01` to `DEV-47`), the go/no-go gates `G1`–`G8`, the performance budgets and the milestone roadmap this PRD turns into product requirements |
| `docs/adr/0002-foundation-and-workflow.md` | How AriadShot is built: repository, toolchain, git and review workflow, and the maintainer's decisions that shape distribution (no paid Apple Developer account) |
| `docs/spec/README.md` and the files it lists | Technical specification for implementers, `01-architecture-overview.md` to `14-gates-and-milestones.md` |
| `parity/ledger/*.jsonl`, `parity/status/*.jsonl`, `parity/deviations.jsonl` | The machine-readable parity ledger, its per-platform status and the deviation register. They are the source of truth for every MacShot value this PRD summarises |

**Conventions.**

- 175 *rows* in nine families (`SH` shell, `OV` overlay, `TB` toolbars, `AN` annotations, `CR` capture and recording,
  `VE` studio, `ED` editor and windows, `SV` services, `ST` settings); *row*, *ledger line*, *domain* and other terms
  are defined in §14. Every FR is traceable to rows; the full coverage index is Appendix A.
- MacShot citations use `path:line@b4d4f3a`, relative to the root of the upstream repository.
- Numbers marked **(H)** are hypotheses until the gate named with them (G3, G5 or G8) measures them; ADR 0001 then
  keeps, revises or rejects each one. Requirements and targets marked **(PRD)** are introduced by this document.

## 1. Vision

AriadShot brings MacShot — the fast, local-first screenshot, annotation, screen-recording and video-editing tool for
macOS — to Linux, with Wayland as a first-class citizen, and back to macOS, as **one GPL-3.0-only C++20 / Qt 6
application**. A person who knows MacShot installs AriadShot and finds the same product: the same capture flows and
overlay, the same 19 annotation tools with the same defaults and shortcuts, the same toolbars, popovers and pixel
geometry, the same editor, history, pin, thumbnail, OCR, translation, redaction, scroll capture, recorder and Studio
video editor, and the same settings. Where an operating system forbids an exact behaviour, AriadShot says so in the
interface and offers the closest legal equivalent.

Product principles, in priority order:

1. **Fidelity is the product.** Constants, defaults, strings, geometry and state machines come from MacShot at
   `b4d4f3a`. Behaviour that MacShot does not have is a defect unless it is a registered deviation.
2. **Measured, not claimed.** Parity is a number per platform, computed from the ledger, golden images against a MacShot
   reference corpus, behavioural trace replays and generated defaults tests.
3. **Local-first and private.** Zero telemetry. Processing happens on the device. The network is used only for the
   purposes listed in §9.5.
4. **Honest degradation.** The interface reflects what the running desktop can do. Nothing fails silently.
5. **The user's desktop belongs to the user.** AriadShot never assumes it owns a key chord and never edits compositor
   configuration without an explicit user action.
6. **Free and open.** GPL-3.0-only, no paid accounts or services, built only from public sources.

## 2. Problem

- **MacShot is macOS-only.** It is built on AppKit, ScreenCaptureKit, Vision, Speech and AVFoundation and requires
  macOS 12.3 or later (`README.md:207-209@b4d4f3a`; `macshot.xcodeproj/project.pbxproj:317@b4d4f3a`). People who use
  Linux, or both systems, cannot run it.
- **Linux Wayland breaks the X11 generation of tools.** Wayland forbids passive global input capture, client-side
  positioning of normal windows and, on GNOME, window lists; clipboard ownership is cooperative. The open-source tools
  studied for this project each cover a subset: Flameshot's capture widget relies on X11 window hints that Wayland
  compositors ignore, so on Hyprland and Sway it is tiled like a normal window
  (<https://github.com/flameshot-org/flameshot> at `2d47806`, `src/widgets/capture/capturewidget.cpp`); Satty is an
  annotation editor that delegates capture to `grim` and `slurp` (<https://github.com/Satty-org/Satty> at `2bcd911`);
  Spectacle is built around KDE (<https://invent.kde.org/graphics/spectacle> at `a09e9c9`). None of them offers
  MacShot's single workflow of overlay capture, re-editable annotations, history, OCR and redaction, recording and a
  Studio editor.
- **The cost today.** Users chain separate programs (capture, annotate, record, edit), lose re-editable annotations and
  history, have no automatic redaction of secrets, and must relearn tools when they switch between macOS and Linux.
- **The target moves.** MacShot shipped more than 150 releases from 2026-03-11 to 2026-09-27. Without a pinned
  baseline "parity" has no fixed meaning, so AriadShot measures against commit `b4d4f3a` until a deliberate re-baseline.

## 3. Personas and users

| Persona | Who | What they need most | Primary platforms |
| :--- | :--- | :--- | :--- |
| **P1 MacShot switcher on Linux** (primary) | A keyboard-driven Linux user on a tiling Wayland compositor who knows MacShot, captures many times a day and has multi-monitor setups with mixed scale and rotation | MacShot's muscle memory: same keys, tools, defaults and speed, overlay above panels on every output, no permission dialogs on every capture | Hyprland (reference), Sway, KDE |
| **P2 Cross-platform professional** (primary) | Works on a Mac and a Linux machine every week | One tool with identical behaviour, portable settings (export/import), identical output files | macOS 14+, Hyprland, KDE |
| **P3 Documentation and support author** (secondary) | Technical writer, developer or QA engineer writing guides and bug reports | Numbered steps, arrows, text, highlight, measure, loupe; PII and secret redaction; scroll capture of long pages; GIFs; pins as references; re-editing from history | all tiers |
| **P4 Demo and tutorial creator** (secondary) | Records product demos and tutorials | Region and full-screen recording with microphone, system audio and webcam; click and keystroke overlays; Studio editing with zooms, cursor smoothing, captions, cuts and speed changes; MP4 and GIF export | Hyprland, macOS |
| **P5 Privacy-conscious user** (cross-cutting) | Security engineer or anyone handling sensitive screens | No telemetry, on-device OCR and redaction, credentials in the keyring, an offline build | all tiers |
| **P6 Multilingual user** (cross-cutting) | Uses one of the 40 UI languages, including right-to-left ones, and types through an input method (for example Vietnamese Telex or a CJK engine) | Full localisation with runtime switching; in-place text annotation that works with their IME | all tiers |

**Stakeholders who are not end users:** distribution packagers (AUR, Flatpak, AppImage, a Homebrew tap), contributors
(humans and AI coding agents working under maintainer review), and the MacShot project, which is credited and whose
material is tracked in `PROVENANCE.md`.

**Not targeted:** Windows, web and mobile users; teams that want a hosted sharing service or accounts; users on macOS
12.3–13.x (DEV-27); users who want features MacShot does not have (NG-4).

## 4. Goals and non-goals

### 4.1 Goals

| ID | Goal (outcome) | Kind | Measured by |
| :--- | :--- | :--- | :--- |
| GOAL-1 | A MacShot user finds the same product: 100% of applicable parity-ledger lines verified on the reference platform at 1.0 | user | SM-1 to SM-4 |
| GOAL-2 | Linux Wayland is first class: an interactive overlay on every output within the latency budget, no prompt per capture on Hyprland, recording without dropped frames | user | SM-5 to SM-9 |
| GOAL-3 | macOS users get the same product on macOS 14+: 100% of applicable lines at 1.0, never more than one milestone behind the reference platform (contingent on gate G7) | user | SM-1 |
| GOAL-4 | No lost work: committed history survives any crash; recordings recover up to the last synced fragment | user | SM-10 |
| GOAL-5 | Private, free and legally clean: zero telemetry, no paid accounts, complete licence provenance | project | SM-12, SM-13 |
| GOAL-6 | Honest breadth: KDE Plasma 6 and X11 reach at least 95% of applicable lines; every gap on any desktop is visible in the interface and registered | project | SM-1, SM-11 |

### 4.2 Non-goals

| ID | Non-goal | Why |
| :--- | :--- | :--- |
| NG-1 | Byte-for-byte compatibility with MacShot files (settings plist, history `index.json` dates, `cursor.mstl`, project JSON) | AriadShot defines versioned cross-platform formats with the same fields and semantics (DEV-28). Importing a MacShot settings export may come later (open question Q12) |
| NG-2 | Windows, web or mobile clients; AriadShot-operated servers, accounts or cloud storage | Out of the product; MacShot has none either |
| NG-3 | Telemetry, analytics, crash reporters or usage beacons of any kind | Privacy principle; matches MacShot's `PRIVACY.md@b4d4f3a` |
| NG-4 | Features MacShot lacks (colour management, JPEG XL, NER-based redaction, LLM translation) before 1.0 | Fidelity first; may return after 1.0 as clearly marked extras |
| NG-5 | Reproducing MacShot's known defects | Fixed and registered instead (DEV-07 to DEV-11, DEV-39) |
| NG-6 | The v4.0.4 "classic" video-editor layout | The baseline's code is the v4.4 Studio editor, which is the target |
| NG-7 | Pixel identity between Linux and macOS | Each platform meets the MacShot corpus within its tolerance class (§9.3); font rasterisers and emoji fonts differ |
| NG-8 | macOS 12.3–13.x | One still-capture path and macOS 14 APIs (DEV-27) |
| NG-9 | Paid distribution: Apple Developer ID signing, notarization, the Mac App Store | "No paid accounts" constraint (C-2) |
| NG-10 | An in-app self-updater on Linux | Linux packaging norms (DEV-36) |
| NG-11 | GNOME overlay parity through a GNOME Shell extension before 1.0 | Evaluated at M5 only (Q11) |

## 5. Success metrics

AriadShot collects no usage data, so every metric comes from repository artifacts: the parity report generated from
`parity/`, CI results, the benchmark harness, robustness runs and the public issue tracker. Adoption signals (AUR votes,
repository stars, release downloads) are informational only and never gate a release.

| ID | Metric | Target | Method | Kind / when |
| :--- | :--- | :--- | :--- | :--- |
| SM-1 | Parity per platform = verified ledger lines ÷ applicable lines | the per-milestone targets of §7.1 | parity report from `parity/` | lagging; each milestone exit |
| SM-2 | Defaults parity: 219 settings keys, 27 single-key shortcuts, 12 hotkey slots | 100% equal to MacShot's effective default (unset kept distinct from default) or registered | generated defaults test | leading; every CI run from M1 |
| SM-3 | Visual fidelity against the MacShot corpus | macOS: CIEDE2000 ΔE00 ≤ 2 on ≥ 99.5% of pixels; Linux: same threshold outside declared tolerance masks, each mask tied to a deviation entry | corpus golden tests | leading; every PR touching rendering |
| SM-4 | Behavioural trace parity | 100% of scripted input sequences yield field-identical annotation models and selection rects | trace replays on MacShot and AriadShot | leading; every PR touching interaction |
| SM-5 | Hotkey → pointer output frozen and interactive (W1, P1) | ≤ 100 ms p50, ≤ 150 ms p95 (H) | benchmark harness | leading; M0 baseline, each milestone |
| SM-6 | Annotation drag or selection resize (W2, P1–P3) | ≤ 16.7 ms p95 (H) | benchmark harness | leading |
| SM-7 | Idle daemon (W0) | ≤ 100 MB RSS, 0% CPU, no overlay buffers retained (H) | benchmark harness | leading |
| SM-8 | 1080p60 recording (W4) | ≤ 15% of one core, zero dropped frames, ≤ 1 CPU pixel copy per frame (H) | benchmark harness | leading; from M3 |
| SM-9 | Studio preview (W5) | 60 fps p95 (H) | benchmark harness | leading; from M4 |
| SM-10 | Robustness gates of §9.4 | all pass | scripted runs | lagging; each milestone exit |
| SM-11 | Undocumented gaps | 0: every applicable line not verified and every excluded line has a deviation entry | parity report | lagging; each milestone exit |
| SM-12 | Unexpected network traffic (PRD) | 0 connections outside §9.5 during a scripted session of every feature, including the offline build | network-audit test | leading; from M2 |
| SM-13 | Licence compliance | 100% of files pass `reuse lint`; every MacShot-derived file listed in `PROVENANCE.md`; 0 models with non-permissive weights in the default catalogue | CI and release checklist | lagging; each release |
| SM-14 | Daily-driver readiness (PRD) | the reference user replaces the desktop's screenshot flow with AriadShot for 14 consecutive days with 0 lost captures and 0 crashes | dogfooding log attached to the M1 release | lagging; M1 exit |
| SM-15 | Post-release quality (PRD) | 0 open issues labelled data loss or security older than 14 days at each release; parity-defect reports reproduced against `b4d4f3a` within 14 days | issue tracker | lagging; from v0.1.0 |

## 6. User stories

Ordered by priority within each persona. "Edge case" names the error, empty or boundary state the story must cover.

| Persona | As a…, I want…, so that… | Edge case | FRs |
| :--- | :--- | :--- | :--- |
| P1 | As a MacShot switcher, I want the capture shortcut to freeze my pointer's monitor and show the overlay at once, so that transient menus and tooltips are still on screen | other outputs freeze progressively | FR-APP-10, FR-CAP-1 |
| P1 | As a switcher with a rotated portrait monitor and a fractional-scale panel, I want selections, including ones spanning both, to be correct and sharp, so that multi-monitor captures just work | mixed device pixel ratios | FR-OVL-11 |
| P1 | As a switcher, I want the same single-key tools (`p`, `a`, `r`, `t`…) and the same Esc behaviour, so that my muscle memory works unchanged | non-Latin keyboard layout | FR-KEY-3, FR-OVL-10 |
| P1 | As a user whose compositor config already binds my screenshot chord, I want AriadShot to request it and let me decide, so that it never steals a binding silently | the bound chord cannot be read back → the row says so | FR-KEY-1, FR-KEY-2 |
| P1 | As a user of a desktop where capture is denied or unavailable, I want an explicit error instead of a silent no-op, so that I know why nothing happened | a CLI capture exits non-zero with the same message | FR-APP-3 |
| P2 | As a cross-platform professional, I want to export settings on one system and import them on the other, so that both behave the same | secrets never exported; a macOS-only snap mode is kept in the file but coerced on Linux | FR-APP-13 |
| P2 | As a cross-platform professional, I want the same annotations to produce the same PNG on both systems within the documented tolerance, so that shared documentation looks consistent | emoji and font raster differ within registered masks | §9.3 |
| P3 | As a documentation author, I want numbered badges, arrows, text and spotlight with MacShot's defaults, so that guides look consistent without tuning | numbering after deletions | FR-ANN-7, FR-ANN-10 |
| P3 | As a support engineer, I want one action that redacts emails, keys, tokens and card numbers, so that I can share screenshots safely | PII runs on shared code with no download; face detection on Linux may first ask to download a model | FR-ANN-9, FR-ML-5 |
| P3 | As a documentation author, I want scroll capture with auto-scroll, so that a long page becomes one stitched image | on KDE and GNOME auto-scroll is unavailable and the control says why | FR-CAP-4, FR-CAP-5 |
| P3 | As a QA engineer, I want to reopen yesterday's capture from history and edit each annotation, so that I can fix a typo without recapturing | a damaged history element is dropped, not the whole entry | FR-OUT-3, FR-ANN-14 |
| P4 | As a demo creator, I want microphone, system audio and webcam recorded as separate tracks, so that I can balance them later | two audio tracks open the merge dialog | FR-REC-4, FR-REC-6 |
| P4 | As a demo creator, I want automatic zooms from clicks, a smoothed cursor and on-device captions, so that a raw take becomes a polished video | on Wayland without the opt-in helper, click-based looks are disabled with a reason | FR-STU-2, FR-STU-3, FR-STU-7 |
| P4 | As a demo creator, I want a recording to survive a crash, so that a long take is not lost | the take reopens marked "interrupted" | FR-REC-3 |
| P5 | As a privacy-conscious user, I want no network traffic unless I upload, translate, check for updates or approve a model download, so that screenshots never leave my machine by accident | the offline build | §9.5 |
| P5 | As a user without a keyring service, I want to be told that credentials sit in a user-readable file, so that I can install a keyring | secrets are never moved silently | FR-NET-2 |
| P6 | As a Vietnamese user, I want to type Telex directly in an overlay text annotation, so that diacritics compose in place | a failing IME/compositor pair moves entry to a pop-up editor and says so | FR-ANN-6 |
| P6 | As an Arabic, Persian or Hebrew user, I want a mirrored interface switchable at runtime, so that I never restart to change language | mixed-direction text in annotations | FR-I18N-1 |

## 7. Scope by milestone

### 7.1 Milestone overview and parity targets

Every milestone ends with four artifacts: the parity-ledger report per platform, the performance budgets that apply,
the robustness gates, and the updated deviation register. Releases follow SemVer: no release for M0, `v0.1.0` at M1
through `v0.4.0` at M4, `v1.0.0` at M5.

| Milestone | Goal | Hyprland (reference) | macOS 14+ (contingent on G7) | KDE Plasma 6 / X11 | GNOME |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **M0** Foundations and gates | prove or kill the load-bearing assumptions | measurements and decisions only | G7 environment | — | — |
| **M1** MVP | a daily-driver screenshot tool on Hyprland | 100% of the M1 atomic set (§7.3) | builds, launches, captures, copies; no parity claim | best effort | best effort |
| **M2** Screenshot parity | every `shot` line | 100% of `shot` lines; every WL-HARD shot line verified or registered | 100% of the M1 atomic set with macOS backends; macOS feature gates 1–4 | 100% of the M1 atomic set (nested KWin, Xvfb/Xephyr) | no claim |
| **M3** Recording parity | the recorder | + 100% of `rec` lines (click and key telemetry through the opt-in helper) | 100% of `shot` lines | ≥ 90% of `shot` lines | no claim |
| **M4** Studio parity | the v4.4 Studio | + 100% of `studio` lines | + `rec` lines (feature gate 5) | + `rec` lines where the backend allows | no claim |
| **M5** Release 1.0 | ship | 100% of applicable lines incl. `release` | 100% of applicable lines (feature gate 6; feature gate 7 applies to the ad-hoc-signed, un-notarized path of FR-REL-2) | ≥ 95% of applicable lines | every line not verified in a GNOME VM is registered |

Sway and other wlroots compositors are claimed only for what headless Sway CI measures (no separate percentage).
"Applicable" excludes MAC-ONLY lines on Linux and lines outside a tier promise; each excluded line has a deviation.

### 7.2 M0 — Foundations and go/no-go gates

| Gate | Proves | Pass criteria (summary; full text in ADR 0001) | If it fails |
| :--- | :--- | :--- | :--- |
| G1 Overlay host | Qt surfaces work as overlays on Hyprland layer-shell | seven steps: one overlay-layer surface per output over the bar (incl. a rotated output and scale 1.25); exactly one keyboard-owning surface with hand-over; in-surface popovers; IME (Vietnamese Telex and one CJK engine, candidate window ≤ 50 px from the caret); re-show ≤ 150 ms p95; drag ≤ 16.7 ms p95; cursor shapes | predefined outcomes: Widgets host H1, Qt Quick host H2 (1–2 weeks), hand-written client H3 (4–6 weeks), IME fallback (DEV-21), or fullscreen-toplevel fallback |
| G2 Capture | the specified capture sequence, not just advertised protocols | per-output source; no AriadShot pixels in 100 captures with a thumbnail and pin visible; rotated output upright; renegotiation and fallback to screencopy; cursor as `captureCursor`; W1 measured | screencopy primary; else a portal snapshot path with revised budget |
| G3 Still rendering contract | preview equals export for stills | representative tools, mesh gradient, calibrated shadow, text with emoji; PNG export equals the canonical image; presentation exact at integer scale | fix inputs; a QPainter-specific failure reopens the stack decision |
| G4 Wayland connections | AriadShot's own connection coexists with Qt's | 10 minutes of captures and unfocused clipboard copies: no stall > one frame; a background copy survives overlay dismissal | shared queue behind a pinned adapter |
| G5 Studio contract | preview equals export for video | decoded frame through QRhi (Vulkan) on screen and offscreen, VA-API encode; ΔE00 ≤ 1 preview vs pre-encode; OpenGL fallback runs | fix; a QRhi failure on both APIs with a passing wgpu spike reopens the stack |
| G6 Recording path | the Linux recording source | W4 on image-copy DMA-BUF and on PipeWire; 200 kill trials | choose the better path; revise the budget with numbers, never silently |
| G7 macOS environment | every macOS claim | a physical Mac on macOS 26 plus a macOS 14 VM as self-hosted runner; MacShot built at `b4d4f3a` for the corpus; overlay skeleton (ScreenCaptureKit, non-activating panel, IME) on both versions. **Amended by the no-paid-accounts constraint: no notarization dry run; instead an inside-out ad-hoc-signed bundle passes `codesign --verify --deep --strict`, the documented first-open steps work after a quarantined download on macOS 26 and 14, and a permission experiment (ad-hoc and self-signed) records whether Screen Recording grants and Keychain access survive a bundle replacement** | macOS stays "builds only"; corpus falls back to published assets with reduced confidence; the minimum narrows to the oldest version that passes |
| G8 Baselines | the budgets | a baseline for every budget on P1–P3 | each budget kept, revised or rejected in ADR 0001 with numbers |

### 7.3 M1 — MVP (Hyprland, English only)

Everything outside the M1 atomic set is **absent**: it does not appear in menus, toolbars, Settings or the Shortcuts
tab. A later-milestone action requested through a URL or the CLI shows the error pill "“<action>” isn't available in
this version of AriadShot" and returns a CLI error with the same text. The atomic set:

- **Shell:** SH-01; SH-02 without the updater; SH-03 (Linux states); SH-07, SH-09, SH-12 to SH-18, SH-21, SH-24, SH-28,
  SH-31 (Linux chords), SH-32; ST-06; SH-30 image route (Open Image…, `open?file=` for images, `.desktop` MIME types for
  PNG, JPEG, TIFF, GIF, HEIC, HEIF, WebP, BMP; GIF opens in the image editor).
- **URL and CLI actions (SH-04):** `capture`, `capture-fullscreen`, `capture-last`, `quick-capture`, `settings`,
  `history`, `open?file=` (images), `edit?id=`.
- **Status menu (SH-08):** Capture Area, Capture Screen, Quick Capture, Capture Last Area, Capture Delay (None, 3, 5,
  10, 30 s — `macshot/AppDelegate.swift:827@b4d4f3a`), Recent Captures, Show History Panel, Open Image…, Open from
  Clipboard, Pin from Clipboard, Settings…, Quit.
- **Hotkey slots (SH-10):** Capture Area, Capture Screen, History, Quick Capture, Capture Last Area, Open from
  Clipboard, Pin from Clipboard, Clear History, with the Linux default table of FR-KEY-1.
- **Single keys (SH-11):** pencil `p`, arrow `a`, line `l`, rectangle `r`, ellipse `o`, marker `m`, text `t`, number
  `n`, censor `b`, highlight `h`, colour sampler `i`, stamp `g`, measure and loupe (unbound by default), move selection
  Space, adjust selection `s`, open in editor `e`, pin `f`, copy, save, invert.
- **Overlay:** OV-01 to OV-08, OV-10 to OV-24 (incl. ratio presets and cross-display stitching), OV-27, OV-28 (confirm,
  quick save, save, save as, pin), OV-29, OV-32 to OV-34, OV-36.
- **Toolbars:** TB-01 to TB-06, TB-10, TB-11, TB-13; TB-07 minus adjust, beautify and remove background; TB-08 cancel,
  move, open in editor, copy, save, pin; TB-12 rows of the M1 tools (marker without Smart, censor without Text Only and
  Auto); TB-14 colour, font and emoji popovers; TB-15 Save menu.
- **Annotations:** AN-01 to AN-05, AN-06a, AN-07, AN-08, AN-09a, AN-11 to AN-14, AN-16, AN-17, AN-17b, AN-18, AN-19
  (default effects and beautify state), AN-20, AN-21.
- **Capture:** CR-01; CR-02 per display (window capture with alpha arrives with beautify in M2); CR-03.
- **Editor and windows:** ED-01 to ED-04 (copy, save, save as, quick save, pin, Done); ED-05; ED-06 copy, save, save
  as, open in editor, pin, rotate and flip, delete, close all, save all to folder; ED-07; ED-08; ED-09 image flavours.
- **Services and settings:** SV-03 (PNG, JPEG), SV-04, SV-05, SV-08; ST-01 with the General, Capture, Shortcuts and
  Tools tabs showing only M1 lines; ST-03, ST-04, ST-05 (menu order, theme presets); TB-02.
- **Infrastructure:** resident daemon, SNI tray, GlobalShortcuts portal registration with the Hyprland protocol as
  fallback, CLI and URL scheme, background clipboard through data-control, an AUR `-git` package.

### 7.4 M2 — Screenshot parity

- **Image features:** beautify (all gradient styles, window and rounded modes, window capture with alpha), effects, all
  image formats; live beautify and effects previews.
- **Recognition:** OCR and QR with the OCR window, translation and the translate overlay, smart marker, text-only
  censor, auto-redaction (PII, faces, people), background removal, and the Linux model-download flow.
- **Capture:** scroll capture with auto-scroll and its HUD.
- **Sharing:** Share, Quick Look and Open With substitutes; reveal in file manager (screenshots); text, RTF and HTML
  pin cards; uploads (imgbb, S3-compatible, Google Drive once AriadShot's OAuth client exists) with the upload toast.
- **App-wide:** the complete Settings window with backup and import; 40 locales with runtime switching and RTL; the
  Desktop Integration page and first-run window; the remaining `shot` lines of SH-04, SH-08, SH-10 and SH-11.
- **macOS:** the M1 atomic set with Carbon hotkeys and MacShot's default chords, NSPasteboard (chosen format + PNG +
  TIFF), `NSStatusItem`, non-activating `NSPanel` overlays on all Spaces, ScreenCaptureKit pointer-display-first
  capture, window snapping, SF Symbols and system fonts, the standard main menu, `ariadshot://` and document types,
  launch at login, the Screen Recording onboarding window; an **ad-hoc-signed** build.

### 7.5 M3 — Recording parity

Region and full-screen recording with countdown; the recording setup bar and popover; HUD, click ring, keystroke pill
and webcam bubble; microphone and system audio as separate tracks and the audio-merge dialog; fragmented MP4 with
crash recovery; cursor and input telemetry with the opt-in Linux input helper; tray stop; "When done" routes; reveal
recordings in the file manager; the self-exclusion strategy, including a test of Hyprland's `no_screen_share` rule. No
GIF *recording*: MacShot writes GIF only in the Studio export (`macshot/UI/Editor/Video/VideoEditorExporter.swift:122`).

### 7.6 M4 — Studio parity

The project model with autosave; the inspector's six sections; stage manipulation; the timeline with all lanes and
snapping; auto-zoom and cursor restyling from telemetry; on-device captions and SRT export; MP4 export and GIF export
through the streaming GIF writer, under the studio contract; copy and upload of videos; the export progress window; the
video open route with its no-telemetry mode.

### 7.7 M5 — Release 1.0

KDE, GNOME, X11 and generic wlroots backends to their tier promises; AppImage and Flatpak (portal-only); the macOS DMG,
ad-hoc signed and not notarized, with Sparkle EdDSA updates; update checks (SH-26) and the offline build (SH-25); macOS
catch-up to full parity; accessibility; performance tuning to every kept budget; the licence release gates (§12.3);
user documentation; a review of upstream MacShot changes since `b4d4f3a`. A GNOME Shell extension is evaluated here,
not before.

### 7.8 Timeline constraints (no calendar dates are committed)

- The hosted `macos-14` CI image retires on 2026-11-02; the macOS 14 VM runner of G7 then takes over (RK-21).
- G7 needs the maintainer's Mac. The final G1, G2 and G8 runs use the physical two-display P1 topology (a portrait
  display is reconnected for them); nested and headless compositors serve development runs.
- Drive and imgbb uploads need AriadShot's own free OAuth client and key, or user-supplied ones (DEV-31).

## 8. Functional requirements

### 8.0 Rules that apply to every requirement

- **FR-GEN-1 One implementation.** Every row tagged PORT has one implementation and one set of constants on all
  platforms. In-app shortcuts use ⌘ on macOS and Ctrl on Linux with the same letters; global hotkey defaults are the
  exception (FR-KEY-1).
- **FR-GEN-2 Values from the ledger.** Every constant, default, string and geometry value equals the MacShot value cited
  in its ledger line. Tests compare against the ledger value, never against an AriadShot constant.
- **FR-GEN-3 Absent means absent.** A feature outside the running build's milestone or capabilities never appears half
  working (see §7.3 for the error pill and CLI error).
- **FR-GEN-4 Capability-driven interface.** A capability registry resolves what the running session can do from
  protocols, portal versions and probes — never from the desktop's name — at start-up and on display changes:
  - never available on this system → the control is hidden (as MacShot hides actions its OS lacks);
  - available after a user action (install the input helper, grant a portal permission, install the GNOME tray
    extension, download a model) → shown disabled with a one-line reason and a "How to enable" link to the Desktop
    Integration page;
  - degraded but working → a dismissible helper badge on first occurrence, in the helper-card style, and a listed
    limitation on the Desktop Integration page;
  - every such case is a deviation entry and a ledger status.
- **FR-GEN-5 Upstream defects are fixed, not copied**, each as a deviation entry: text re-edit undo restores the
  original (DEV-07); crop and rotate undo move annotations back (DEV-08); custom colour slots persist (DEV-09);
  `beautifyBgRadius` is honoured in preview and export, where MacShot always uses 0 (DEV-10); the countdown releases the
  capture gate when no screen is available (DEV-11); an empty text re-edit is one undoable removal (DEV-39).
- **Milestone column.** It names the reference platform's milestone; macOS and the other tiers follow the bars of
  §7.1 (for example, a `shot` row outside the M1 atomic set reaches macOS at M3).
- **Acceptance.** An FR is accepted on a platform when every ledger line under its rows is `verified`: implementation
  merged; named tests pass in CI or in a named environment with its log attached; asserted values equal the cited
  MacShot values; the line's evidence class is met (visual → corpus golden, interaction → trace script, default →
  defaults test, timing → frame-timed measurement); differences are registered; a second reviewer checked the cited
  source; provenance, translations and accessible names updated. AriadShot-defined behaviour, which has no MacShot
  value, is accepted by the scenarios of §8.17.

### 8.1 Application shell and lifecycle

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-APP-1 | A resident daemon starts at login when enabled, owns tray, hotkeys, clipboard selections and pre-warmed overlays, holds no screenshot pixels while idle, and is single-instance: a second launch signals the primary (show icon and open Settings) and exits | SH-01, ST-06 | M1 | Linux: local socket in a 0700 runtime directory, XDG autostart; macOS: `SMAppService` |
| FR-APP-2 | Launch runs MacShot's ordered start-up sequence (migrations, save-failure toasts, background cleanup, history init with orphan prune, updater, menus, status item, hotkeys, audio pre-warm, observers, capability or permission check, overlay pre-warm) and the TTL sweepers (editor clones, temporary files 24 h, share scratch 5 min, legacy folders) | SH-02, SH-24 | M1 (updater M5) | — |
| FR-APP-3 | Two request queues: a cold-launch queue drained after start-up, and a capture-class queue held until capture is possible and dropped if it cannot be (§8.17 S1) | SH-03 | M1 | macOS: held until Screen Recording permission, dropped if onboarding closes without it |
| FR-APP-4 | `ariadshot://` URL actions `capture`, `capture-fullscreen`, `capture-last`, `quick-capture`, `ocr`, `ocr-translate?target=`, `record`, `record-fullscreen`, `scroll-capture`, `settings`, `history`, `open?file=`, `edit?id=`, `stop-recording`, enabled by default (`urlSchemeEnabled`), and a Qt-free `ariadshot` CLI with the same actions | SH-04 | M1 subset; M2 OCR and scroll; M3 recording; M4 video open | Linux `x-scheme-handler`; macOS `CFBundleURLTypes`; `open?file=` uses the file router of FR-APP-18 (DEV-46) |
| FR-APP-5 | App presence: an accessory app with no Dock icon that becomes a regular app while the editor, Studio or Settings is open, returns focus to the previous app on close, and offers a Dock menu of windows | SH-05, SH-06 | M2 (Linux substitute); macOS M3 | Linux: normal windows; no Dock menu (DEV-35); focus return through compositor IPC where available |
| FR-APP-6 | Status item: 22×22 pt template icon (default asset or custom symbol), `hideMenuBarIcon`; forced visible while recording, shows a stop symbol, click stops recording | SH-07 | M1 | Linux: StatusNotifierItem; GNOME needs the AppIndicator extension (DEV-26) |
| FR-APP-7 | Status menu with MacShot's items, order and submenus: six reorderable capture items, Capture Delay, Record Area, Record Screen, Recent Captures ("W × H · time ago", thumbnail, click copies, Clear History with confirm, empty text), Show History Panel, Open Image…, Open Video…, Show Recordings, Open from Clipboard, Pin from Clipboard, Settings…, Check for Updates…, Quit. Shortcut labels hide for disabled hotkeys and are rebuilt when the keyboard layout changes | SH-08, SH-09 | M1 subset (§7.3); rest M2–M5 | "Show Recordings in Finder" becomes "Show Recordings" in the file manager on Linux |
| FR-APP-8 | Capture gate: one capture at a time and none while recording; session IDs discard stale callbacks; previous app and window title remembered; tools reset when `rememberLastTool` is off; AriadShot's own windows hidden during capture and restored under a generation guard so stale windows never flash into the next capture | SH-13, SH-32 | M1 | window title: backend-dependent |
| FR-APP-9 | Pre-capture countdown: 140×140 floating window (on the selection for recordings, mouse-transparent), 120 pt circle black 0.7, 3 pt ring white 0.6, 52 pt monospaced-digit bold; Esc cancels and clears all pending flags | SH-14 | M1 | — |
| FR-APP-10 | One pre-warmed overlay per display, rebuilt on display change; progressive multi-display capture with the pointer display first and interactive immediately | SH-15, SH-16 | M1 | GNOME: the portal returns one whole-desktop image, so pointer-first is impossible (DEV-26) |
| FR-APP-11 | Output routes: Confirm (Copy, ⌘C, double-click) always copies; Quick Save (Return, quick-capture mouse-up) follows `quickCaptureMode` 0 save, 1 copy (default), 2 save+copy, 3 nothing, 4 save+copy path, optionally opening the editor. Quick Capture hides toolbars and saves on mouse-up. Capture Last Area applies only to a display with an identical frame | SH-17, SH-18, SH-21 | M1 | — |
| FR-APP-12 | First-run guidance: MacShot's permission onboarding window (400×520, 610 for returning users, 0.75 s polling, deep link, 1 s auto-advance) | SH-22 | M2 | Linux substitute: a first-run window in the same geometry showing capture capability states (DEV-30) |
| FR-APP-13 | Settings export and import: JSON envelope (type `ariadshot-settings`), fail-closed secret filter, machine-key exclusions, 2 MiB data cap, replace-portable import | SH-23 | M2 | — |
| FR-APP-14 | An offline build variant removes upload and cloud code and keeps update checks | SH-25 | M5 | Linux: also no model downloads; features offer "Import model file…" (DEV-32) |
| FR-APP-15 | Updates: signed appcast, daily automatic check (`macshot/Info.plist:58-61@b4d4f3a`), no automatic download, beta channel | SH-26 | M5 | macOS: Sparkle 2 with EdDSA; Linux: check-only notice linking to the package manager (DEV-36) |
| FR-APP-16 | Capture sound through a pre-warmed audio output; `playCopySound` on by default; AriadShot's own sound (DEV-01) | SH-28 | M1 | — |
| FR-APP-17 | Diagnostics: SIGTERM diagnostic log and App Nap suppression | SH-29 | M2 (Linux crash log); macOS M3 | Linux: a crash log in the state directory only (DEV-47) |
| FR-APP-18 | Open routes: image files (PNG, JPEG, TIFF, GIF, HEIC, HEIF, WebP, BMP, `.icns`) open in the image editor, GIF deliberately so; `mp4`/`mov`/`m4v` open in the Studio; entry points Open With, drop, CLI, Open Image…, Open Video… and the `open?file=` URL action. A non-take video opens without telemetry, with cursor, zoom-from-clicks and keystroke looks disabled and the reason shown | SH-30 | M1 images; M4 video | Linux `.desktop` MIME list; macOS document types (`macshot/Info.plist@b4d4f3a`); MacShot's URL action always opens the image editor (DEV-46) |
| FR-APP-19 | Standard window chords: About, Quit, Close Window, Undo and Redo with dynamic chords, Cut, Copy, Paste, Delete, Select All | SH-31 | M1 | Linux: no global menu; each window wires Ctrl+W, Ctrl+Q (closes the editor or Settings window, not the daemon) and edit chords (DEV-12) |
| FR-APP-20 | Reveal in file manager for history items, the image context menu and recordings | SH-34 | M2 (`shot`), M3 (`rec`) | Linux: `org.freedesktop.FileManager1.ShowItems`, else open the folder through the OpenURI portal |

### 8.2 Global shortcuts and keyboard

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-KEY-1 | Twelve global hotkey slots (Capture Area, Capture Screen, Record Area, Record Screen, History, Capture OCR, Quick Capture, Scroll Capture, Capture Last Area, Open from Clipboard, Pin from Clipboard, Clear History) with a per-slot disable flag; F-keys may be bare; opening a hotkey action closes modal windows; only slots whose features exist in the build are registered | SH-10 | M1 subset; M2 OCR, scroll; M3 recording | macOS defaults ⌘⇧X/F/R/H/T/S (Carbon). Linux (DEV-13): Capture Area *requested* as Ctrl+Shift+4; Ctrl+Shift+3 and Ctrl+Shift+5 suggested but unbound; all others unbound. Backends: GlobalShortcuts portal first, Hyprland global-shortcuts protocol as fallback, `XGrabKey` on X11, the CLI everywhere |
| FR-KEY-2 | Shortcuts-tab recorders for hotkeys, chords and single keys with validation, clear and reset | ST-03, SH-10 | M1 | Wayland: "request, then read back" (DEV-14, §8.17 S3). Never steals a chord silently; on Hyprland, lists existing binds using the same chord |
| FR-KEY-3 | 27 single-key overlay and editor shortcuts (26 in the offline build) with layout-aware character matching, including non-Latin layouts; an empty value disables the key | SH-11, SV-08 | M1 subset (§7.3); M2 rest | Linux: xkbcommon keymap translation |
| FR-KEY-4 | Editor undo ⌘Z, redo ⌘⇧Z and ⌘Y; customisable chords that require ⌘ and take precedence over conflicting chords | SH-12 | M1 | Ctrl on Linux |

### 8.3 Capture overlay and selection

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-OVL-1 | One overlay per display above panels showing the frozen frame, dimmed black 0.45 outside the selection (`disableSelectionOutsideShadow`) | OV-01 | M1 | layer-shell (host chosen by G1); `NSPanel` level 257; X11 override-redirect; GNOME fullscreen window (DEV-26) |
| FR-OVL-2 | MacShot's overlay state machine (idle, selecting, selected; editor, scroll-capturing, recording, auto OCR, auto quick-save, auto record, auto translate flags), its delegate output API (~29 callbacks), its 24-pass draw order and its hit-test pass-through rules | OV-02, OV-03, OV-24, OV-36 | M1 | — |
| FR-OVL-3 | Idle helper card (centred, black 0.65, r 8, pad 14, 13 pt medium, snap-mode line in green or orange, `hideCaptureInstructions`), selecting badge ("Hold Space to move. Release to annotate and edit", quick-save variant) and the 34×28 pre-selection preset button | OV-04, OV-05, OV-06 | M1 | Linux card text per capability (DEV-15) |
| FR-OVL-4 | Snap modes cycled by Tab window → off → element, persisted as `captureSnapMode`, synced across displays. Window snapping highlights the window under the pointer (systemBlue 0.08 fill, 0.85 2 pt r 4) and a click selects it and grabs an alpha-cornered window image for beautify. Element snapping follows the accessibility tree | OV-07, OV-08, OV-09 | M1 (window image M2) | Window snapping: macOS, Hyprland and Sway IPC, X11 EWMH; KDE and GNOME boundary snapping only (DEV-17). Element snapping: macOS only (DEV-16). Tab skips unavailable modes (§8.17 S4) |
| FR-OVL-5 | Boundary snapping (edge index built off-thread, ±4 pt query, mean ≥ 28, support ≥ 0.55; suppressed by Option, ratio preset, Shift, Space), auto-adjust selection, 5 pt alignment snapping with dashed cyan guides, and auto-measure while holding 1 or 2 (`measureClampToSelection` default true, `macshot/UI/Overlay/OverlayView.swift:580@b4d4f3a`) | OV-10, OV-11, OV-12, OV-13 | M1 | — |
| FR-OVL-6 | Colour sampler pill ("#RRGGBB", "Right-click to copy") writing custom colour slots, and the right-click radial colour wheel (16 swatches, ring r 72) | OV-14, OV-19 | M1 | — |
| FR-OVL-7 | Selection chrome (2 pt accent border, 2.5 pt red in scroll capture; eight 10 pt handles with 14 pt hit areas; cursor waterfall incl. the hidden cursor under the pencil dot) and modifiers (Shift 1:1, Option from centre, Space rigid move, arrows 1 pt / Shift 10 pt, right-click anchored selection) | OV-15, OV-16 | M1 | cursor shapes per backend |
| FR-OVL-8 | Resolution box (width and height fields 1…100,000, px/pt toggle, presets button, placement candidates, reserved-zone avoidance) and ratio presets (9 ratios, freeform, live custom, 7 fixed sizes, keep ratio, Auto Adjust) | OV-17, OV-18 | M1 | the macOS notch maps to reserved zones on Linux |
| FR-OVL-9 | Mouse semantics: double-click inside rewinds the first click's annotations then confirms; outside confirms; a single click outside is inert; double-click on text edits it | OV-20 | M1 | — |
| FR-OVL-10 | Overlay keys: Tab, F, R, the seven-level Esc waterfall, Return/Enter quick save, Delete, ⌘C, ⌘V and ⌘D (+15/−15 offsets), ⌘Z/⌘⇧Z, ⌘S, ⌘⇧S, hold 1/2 | OV-21 | M1 | — |
| FR-OVL-11 | Multi-display: global coordinates, remote selection chrome on other displays, resize from any display; cross-display stitching at each output's native resolution with the mixed-DPR rule | OV-22, OV-23 | M1 | improvement over MacShot's primary-scale truncation (DEV-34) |
| FR-OVL-12 | Live beautify preview with the toolbar anchor animation (60 Hz, +0.08 per tick, ease-out) and effects preview clipped to the selection | OV-25, OV-26 | M2 | — |
| FR-OVL-13 | Error pill: red 0.8/0.2/0.2/0.9, r 8, 13 pt, 40 pt below the top, 4 s | OV-27 | M1 | — |
| FR-OVL-14 | Finish flows: confirm (effects, then beautify, window-snap annotations on the window image, always clipboard), quick save, save as (cancel restores the overlay), file save (sound after the write), pin, OCR, upload, share (0.5 s re-entry throttle) | OV-28 | M1 subset; M2 rest | Share: macOS only, hidden on Linux (DEV-18) |
| FR-OVL-15 | Dismiss keeps the overlay alive and releases images; teardown only on display change or quit; the last selection is saved when larger than 1×1 | OV-29 | M1 | — |
| FR-OVL-16 | Undo entries (added, deleted at index, image transform, property change); editor transforms (flip with annotation mirroring, add capture below with canvas auto-expansion, invert, crop commit); canvas ↔ view zoom transforms | OV-32, OV-33, OV-34 | M1 | — |
| FR-OVL-17 | Recording outline (click-through, 1.5 pt accent 0.8 outside the crop); recording popover (240 wide; session-only FPS 15/30/60/120; When done; Delay; Hide controls; persistent webcam position, size, shape) and the other overlay popovers (upload confirm, redact categories, translation languages, gradient grid, effects) | OV-30, OV-31 | M2 (non-recording popovers); M3 | translation languages: the installed Apple languages on macOS, the configured engines on Linux |
| FR-OVL-18 | Scroll-capture state: transparent hole, red border, HUD | OV-35 | M2 | — |

### 8.4 Toolbars and chrome

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-TBR-1 | Solid-fill theme: accent #8C4DD9, icons white, background #1F1F1F, light controls when background brightness > 0.5; presets Default, Classic, Ocean, Sunset, Forest, Mono, with Custom auto-selected when colours differ by more than 0.01 | TB-01, TB-02 | M1 | — |
| FR-TBR-2 | Strip geometry (32×32 buttons r 6, padding 4, spacing 2), button states and hover edge cases, 14 pt medium tinted icons, instant tooltips (11 pt medium; shortcut suffix only with `showToolShortcutsInTooltips`, default off) | TB-03 to TB-06 | M1 | icons: SF Symbols on macOS; a tuned Lucide-based set on Linux, approved per icon (DEV-02) |
| FR-TBR-3 | Bottom bar (pencil … redo, then optional invert, adjust, beautify, remove background), capture right bar (cancel, move, open in editor, copy, save, then optional share, upload, pin, OCR, translate, scroll capture, record) and recording-setup right bar (start, cancel, click highlight, keystrokes, system audio, microphone with level and device menu, webcam, settings, move) | TB-07, TB-08, TB-09 | M1 subset; M2; M3 | devices per backend |
| FR-TBR-4 | Customisation through `enabledTools` (17 values) and `enabledActions` with migration guards so disabled items stay disabled | TB-10 | M1 | — |
| FR-TBR-5 | Options row (34 tall, min width 200) with each tool's exact controls, ranges, procedural icons and defaults; edits apply to the selected annotation with one undo snapshot on deselect | TB-11, TB-12 | M1 (M1 tools); M2 rest | — |
| FR-TBR-6 | Toolbar placement algorithm (50 pt fit margin, narrow-selection fallback, bottom bar below/above/inside, overlap resolution, resolution-box and reserved-zone avoidance) | TB-13 | M1 | — |
| FR-TBR-7 | Popovers (colour picker 186×312 with 12 presets and 7 custom slots; font picker with 18 curated plus system fonts; emoji picker with 5 categories and 104 emoji; beautify, effects 250×317 and recording-settings pickers) and right-click menus (Save, Keystrokes, Mic, Webcam, Upload confirm, Translate language) | TB-14, TB-15 | M1 (colour, font, emoji, Save); M2; M3 | curated fonts per platform (DEV-03) |

### 8.5 Annotation model and tools

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-ANN-1 | 19 tool kinds with MacShot's raw values (pencil 0 … highlight 18, incl. legacy blur 10); vector-first annotations with cached rasters, clone, move, bounds and rotation-aware 8 pt hit testing | AN-01, AN-02 | M1 | — |
| FR-ANN-2 | Arrow: head-length formula, six styles (single, thick, double, open, tail dot, sketchy with a persisted seed), straight, legacy bend and multi-anchor Catmull-Rom with right-click waypoints, flip | AN-03 | M1 | — |
| FR-ANN-3 | Rectangle, filled rectangle and ellipse: stroke / stroke + fill / fill, outline, radius 0–30, solid/dashed/dotted with fitted dashing | AN-04 | M1 | — |
| FR-ANN-4 | Pencil (Smooth and Refined smoothing, pressure width, Shift axis lock after 5 pt from the anchor) and plain marker (alpha 0.35, width ×6, same Shift lock) | AN-05, AN-06a | M1 | pressure input per backend |
| FR-ANN-5 | Smart marker: always horizontal and snapped to OCR text lines | AN-06b | M2 | OCR engine per platform |
| FR-ANN-6 | Text: 20 pt default (8–200), bold, italic, underline, strikethrough, alignment, background and outline pills, outside glyph stroke, rich-text source of truth, scoped typing undo; Enter inserts a newline, click outside commits, Esc cancels | AN-07 | M1 | in-place IME required; fallback only for named combinations (DEV-21, §8.17 S5) |
| FR-ANN-7 | Number badges: size rule, pointer cone, decimal/roman/A–Z/a–z formats, next = max + 1 or start value | AN-08 | M1 | — |
| FR-ANN-8 | Censor drawn by hand (pixelate, blur, solid, erase) and "Draw: Text Only" restricted to OCR boxes | AN-09a, AN-09b | M1; M2 | — |
| FR-ANN-9 | Auto-redaction: All Text, PII (11 categories with multi-observation joining and contextual CVV), Faces, People | AN-10 | M2 | PII is shared code on every platform, with ASCII digits only (DEV-45); detectors per platform (DEV-06) |
| FR-ANN-10 | Loupe (40–320, default 120; 1.1–6×, default 2; rooted two-circle mode), measure (px/pt, label rules), highlight/spotlight (dim 0.1–0.95, default 0.55, dashed border by default), stamp (16–256, default 64; 17 quick emoji; picker; custom PNG/JPEG/WebP/SVG; capture stamps) | AN-11 to AN-14 | M1 | emoji font per platform (DEV-04) |
| FR-ANN-11 | Translate-overlay annotation (average background colour, contrasting text, shrink to fit) | AN-15 | M2 | — |
| FR-ANN-12 | Crop tool (editor only) and colour-sampler tool | AN-16 | M1 | — |
| FR-ANN-13 | Annotation selection chrome (10 pt handles, 8 pt bend handles, rotation handle with Shift 45°, delete and edit buttons, multi-select pill, lasso, Ctrl multi-select, Option draw-through, 0.3 s long-press) and the per-tool modifier table | AN-17, AN-17b | M1 | — |
| FR-ANN-14 | Persistence: lenient decoding (only `tool` required; a bad element is dropped, not the document), ~38 fields, group IDs and seeds, saved-capture limits (16,384 px side, 128 Mi px, 128 MiB, RTF 4 MiB, finite coordinates ≤ 1e6); per-revision edit state (effects and beautify) normalised on load | AN-18, AN-19 | M1 | own versioned format (DEV-28) |
| FR-ANN-15 | Colour palette of 12 presets and 7 custom hex slots filled by the sampler; 18 curated fonts followed by all system families | AN-20, AN-21 | M1 | curated list per platform (DEV-03) |

### 8.6 Capture engine and scroll capture

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-CAP-1 | Freeze before focus changes so open menus and tooltips are captured; cursor composited only when `captureCursor` is on (default off) | CR-01 | M1 | — |
| FR-CAP-2 | Per-display capture, pointer display first, never containing AriadShot's own pixels; on failure renegotiate once, fall back to the secondary protocol, else show "Couldn't capture <output>" while other outputs still open; window capture with real alpha corners and shadow for window-snap beautify | CR-02 | M1 (window capture M2) | Hyprland image-copy, screencopy fallback; KDE image-copy or ScreenCast; GNOME Screenshot portal; X11 XShm; macOS ScreenCaptureKit. Window capture: Hyprland, other wlroots compositors where the foreign-toplevel capture source exists, X11, macOS; not KDE or GNOME |
| FR-CAP-3 | Overlay previews downscaled to at most 1,400 px for memory; exported pixels unchanged | CR-03 | M1 | — |
| FR-CAP-4 | Scroll capture: settle on two identical frames (30 tries, 10→80 ms backoff), scrollbar scan, sticky-header lock, vertical registration with the ≥ H/10 rule and −1 px seam bias, stop at 30,000 px (`scrollMaxHeight`), 8 misses or 6 zero shifts | CR-04 | M2 | shared algorithm on every platform |
| FR-CAP-5 | Auto-scroll with pointer warp and synthetic wheel bursts at speeds 1–4 (default 3); manual mode grabs every 150 ms plus 250 ms settle | CR-05 | M2 | Hyprland and wlroots virtual pointer, X11 XTest, macOS events; KDE and GNOME manual only (DEV-25). Wayland manual mode keys on frame changes, not other clients' wheel events (DEV-41) |
| FR-CAP-6 | Scroll HUD with strip count, size, Auto Scroll and Stop, and a 200 pt live preview panel | CR-06 | M2 | — |

### 8.7 Output, saving and history

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-OUT-1 | Encoding: PNG (default), JPEG, HEIC, WebP (≤ 16,383 px, straight alpha), AVIF at quality 0.85, optional 1× downscale; the clipboard carries the chosen format plus PNG with a generation guard and the same bytes as a file export | SV-03 | M1 PNG/JPEG; M2 rest | macOS adds TIFF to the pasteboard; HEIC optional on Linux. Wayland clipboard: while focused, else data-control on Hyprland and KDE; GNOME copies before focus loss (DEV-26) |
| FR-OUT-2 | Saving: save folder or ask, filename tokens and subfolders with a live preview on a fixed sample date, a 200-byte name sanitiser, atomic no-overwrite publish with "(n)" suffixes, export cancellation, source leases and per-item scratch folders | SV-04, ST-04 | M1 | — |
| FR-OUT-3 | History: revision folders (image, thumb, preview, raw, annotations, edit state), atomic index, strict validation, cleanup only when the index is fully valid, `historySize` 10 by default or unlimited, order by last edit, bounded save queue (32 items / 512 MiB), editable restore | SV-05 | M1 | ISO-8601 dates in AriadShot's format (DEV-28) |

### 8.8 Image editor and secondary windows

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-WIN-1 | Detached image editor ("… Editor · HH:mm:ss", min 800×400, fit zoom ≤ 1, zoom 0.1–8), 32 pt top bar (size label, crop, flips, add capture, Done when dirty, zoom menu), dirty tracking with a "Save changes?" sheet, and outputs that mirror the overlay | ED-01 to ED-04 | M1 (share, upload, OCR M2) | Share hidden on Linux (DEV-18) |
| FR-WIN-2 | Floating thumbnail (240×160 × scale, r 12; corner buttons; Copy/Save pills; stacking per corner; 5 s auto-dismiss paused on hover; edge-drag and swipe dismiss; drag-out as a file in the chosen format) | ED-05 | M1 | GNOME cannot place it or keep it on top (DEV-26) |
| FR-WIN-3 | Shared image context menu: copy, save, save as, open in editor, pin, upload, Quick Look, OCR, rotate and flip persisted to history, Open With, Share, Delete, Close All, Save All to Folder | ED-06 | M1 subset; M2 | Linux: Share hidden; Quick Look opens a pin-style preview closed by Space or Esc; Open With uses the OpenURI chooser (DEV-18) |
| FR-WIN-4 | History panel: backdrop and 240 pt panel sliding in 0.12 s, tabs All/Screenshots/GIFs, trash with confirm, 200×160 cards, LRU 150 + 12 lookahead, keyboard navigation, drag to apps, dismiss on deactivation | ED-07 | M1 | GNOME: normal window (DEV-26) |
| FR-WIN-5 | Pin window (≤ 80% of the screen, movable, cursor-anchored zoom 0.1–5, close/edit/zoom pill, three-item menu, no opacity control) and pin from clipboard (images; RTF, RTFD, sanitised HTML and plain text rendered as cards up to 20,000 characters) | ED-08, ED-09 | M1 images; M2 text cards | GNOME: no on-top guarantee (DEV-26) |
| FR-WIN-6 | OCR result window 720×460: preview column, translate header (30 languages), editable monospaced text with find, QR rows with Copy and Open Link, AI Search, Copy ⌘↩ | ED-10 | M2 | — |
| FR-WIN-7 | Upload toast (380×56 top centre, slide 0.3/0.35 s, 8 s / 6 s dismissal), export progress window (420×130, modeless, Esc cancels) and audio-merge dialog | ED-11, ED-12 | M2; M3 (merge); M4 (export) | GNOME cannot place the toast top-centre (DEV-26) |

### 8.9 Beautify and effects

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-BTY-1 | Beautify: window and rounded modes, padding 48, radius 10, shadow 20, background radius 8, 18 mesh and the linear gradient styles, custom image with blur 0–50, synthetic title bar, calibrated ambient and contact shadows cast from the rounded alpha | SV-01 | M2 | every style on every platform, including the mesh styles MacShot lacks on macOS 14 (DEV-44) |
| FR-BTY-2 | Effects: none, noir, mono, sepia 0.8, chrome, fade, instant, vivid; brightness, contrast, saturation, sharpness; legacy vivid migration | SV-02 | M2 | Linux preset looks are LUTs fitted to MacShot renders (DEV-05) |

### 8.10 Recognition, translation and on-device models

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-ML-1 | Quick OCR: mouse-up runs text and QR recognition; `ocrAction` 0 window + copy, 1 window, 2 copy | SH-19 | M2 | macOS Vision; Linux ONNX OCR with script routing, Tesseract fallback; QR restricted to QR and Micro QR everywhere (DEV-06) |
| FR-ML-2 | OCR-translate action: recognise, translate and draw translated text in place | SH-20 | M2 | — |
| FR-ML-3 | Translation with the Google endpoint by default and 30 target languages; the engine section appears only where an alternative engine exists | SV-06, ST-07 | M2 | macOS 15+: Apple Translation; Linux: optional offline engine, and the section lists the Linux engines (DEV-37) |
| FR-ML-4 | Background removal, face and person detection feed FR-ANN-9 and the remove-background action; results may differ between platforms, geometry and planning are shared | AN-10 | M2 | Linux models are permissively licensed (C-9) |
| FR-ML-5 | On-demand model downloads (Linux): consent sheet with model name, purpose, size, weights licence and source; progress ring with cancel that deletes partial data; SHA-256 verification; failure pill with retry; management (status, size, licence, delete, re-download) on the Desktop Integration page | SH-33 | M2 | macOS needs no downloads (DEV-20); offline build: sideload only (DEV-32) |

### 8.11 Recording

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-REC-1 | Recorder lifecycle idle → preparing → recording ⇄ paused → stopping with session UUIDs and a monotonic clock minus pauses; one folder per take (movie, cursor telemetry, optional camera track, status recording/complete/interrupted) | CR-07, CR-08 | M3 | own formats (DEV-28) |
| FR-REC-2 | Normal mode bakes cursor and live overlays; editable mode hides the cursor, records telemetry and the webcam separately, and re-bakes the webcam if its recorder fails | CR-09 | M3 | exclusion of own surfaces per FR-REC-10 |
| FR-REC-3 | Writer: H.264 High (Main for Low quality), no B-frames, 1 s keyframes, BT.709, bitrate = w·h·fps·bpp (0.12/0.22/0.40) × taper × codec factor within per-quality clamps; fragmented MP4 during recording, published as a non-fragmented fast-start MP4; interrupted takes recovered on next launch | CR-10 | M3 | about 1 s fragments with `fdatasync` at most every 2 s and telemetry synced on the same timer, instead of 10 s (DEV-29); hardware encoders per platform |
| FR-REC-4 | Only complete frames; a 1 fps heartbeat after 1 s without frames; microphone as the first audio track (mono AAC 128k), system audio second (stereo AAC 256k); 10 s / 4,096-buffer queue; 5 s pre-roll | CR-11 | M3 | — |
| FR-REC-5 | Failure states: no frames for 10 s, audio overload, audio format change, append failure, 15 s finalisation watchdog, free disk < 256 MB (checked every 2 s), stop before sleep | CR-12 | M3 | — |
| FR-REC-6 | Audio-merge dialog (380×160, two 0–1 sliders, Keep Separate / Merge Audio) with video stream copy and gains divided by max(1, Σ) | CR-13 | M3 | — |
| FR-REC-7 | Cursor telemetry (move, button, shape, key, start, pause, resume; truncation tolerant; 120 Hz; stationary compression; deduplicated shapes) and the cursor motion model (critically damped spring, sway, press dip and release overshoot, idle and typing fades) | CR-15, CR-16 | M3 | Wayland clicks and keys need the opt-in input helper (DEV-24) |
| FR-REC-8 | Live click ring (systemYellow, 0.3 s), keystroke pill (bottom centre +40, 28 pt, 1.5 s hold, 0.6 s fade, shortcuts-only default) and webcam bubble (80–480 pt, default 120, circle or rounded rectangle, four corners at 12 pt) | CR-17, CR-18, CR-19 | M3 | Wayland: ring and pill need the helper (DEV-24); in editable mode the bubble, ring and pill are hidden where they would lie over the recorded region (DEV-23); Linux key names in the pill (DEV-43) |
| FR-REC-9 | Recording HUD (164×32 r 10: stop, pause/play, dot, timer, drag handle; 8 pt above the region with flip; `hideRecordingHUD`) and the status-item stop button | CR-20, CR-21 | M3 | GNOME cannot place the HUD or show a tray without the extension (DEV-26) |
| FR-REC-10 | AriadShot's own HUD, outline and chrome never appear in recordings | CR-23 | M3 | macOS exclusion list; Linux per §8.17 S6 (DEV-22, DEV-23) |
| FR-REC-11 | "When done" routes: open the editor, show in the file manager, copy to the clipboard (`recordingOnStop`) | CR-24 | M3 | copying a video on Wayland needs data-control or focus |

### 8.12 Studio video editor

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-STU-1 | Procedural project model over a read-only source (trim, crop ≥ 0.05, looks, zooms, censors, texts, cuts, speeds, freezes, captions), saved beside the take or in a hashed external folder; 200-snapshot undo with gesture grouping; autosave after 1.2 s; synchronous save on quit | VE-01, VE-13 | M4 | own format (DEV-28) |
| FR-STU-2 | Looks: frame (background gradient, colour, image or wallpaper, blur, eight aspect options, padding 0.08, radius 14 at 1080p, shadow 0.6, hairline border), cursor (system/dot/ring, size, smoothing, hides, click effects, press bounce, motion blur, loop, sway), zoom (1.8 in 1.2–5, gentle/smooth/snappy, connect within 1.2 s, motion blur, dead zone), keystrokes, camera and captions (nine anchors, 44 pt captions, max 7 words) | VE-02 to VE-05 | M4 | wallpaper background: current wallpaper where readable, else hidden on Linux (DEV-19); Linux key names in keystroke looks (DEV-43) |
| FR-STU-3 | Segments (zoom, censor, text, cut, speed 0.25–10 with varispeed audio, freeze 0.1–30) and auto-zoom planning from clicks and typing bursts | VE-06, VE-07 | M4 | auto-zoom needs click telemetry (DEV-24 on Wayland) |
| FR-STU-4 | Renderer layer order: censors on source, crop and frame, camera transform with motion blur, text, pointer effects, captions, keystrokes, webcam; preview equals export under the studio contract (§9.3) | VE-08 | M4 | Vulkan with OpenGL fallback on Linux; Metal on macOS |
| FR-STU-5 | Window (980×640 min, top bar 52, inspector 56 + 304 with six sections, stage toolbar 44, transport 50, timeline 230), keys (Space, arrows, Home/End, I/O, Z/C/T, J/K/L, =/−, ⌘Z/⌘⇧Z/⌘S/⌘⇧S/⌘C/⌘E; ignored while editing text), timeline interactions (scrub, trim, move, edge resize, 7 pt snapping, greedy rows) and stage manipulation (handles, aspect-locked zoom rects, pinning, inline text) | VE-09 to VE-12 | M4 | — |
| FR-STU-6 | Export: render only when needed; MP4, or GIF (5–30 fps, default 15) through the streaming GIF writer (per-frame palettes, changed-rectangle deltas, disposal 1, held duplicates, 100 Hz delays); scale ≤ 1; SRT with time remapping; Esc cancels; modeless progress; copy and upload of the result | VE-14, CR-14 | M4 | encoders per platform; high-quality MP4 uses MacShot's own encoding plan through FFmpeg instead of Apple's export preset (DEV-42) |
| FR-STU-7 | Captions generated, regenerated, exported and removed on device, with the privacy note that nothing is uploaded | VE-15, CR-22 | M4 | macOS Speech; Linux whisper.cpp with a downloaded model (FR-ML-5) |

### 8.13 Uploads and credentials

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-NET-1 | Uploads to imgbb (default provider, images only), Google Drive (OAuth with PKCE, idempotent upload, linear retry) and S3-compatible storage (SigV4; R2, MinIO, B2) with streamed bodies, progress and the upload toast; the offline build removes them | SV-07, ED-11 | M2 (videos M4) | AriadShot's own free credentials or user-supplied keys; "not configured" until they exist (DEV-31) |
| FR-NET-2 | Credentials (S3 keys, a user imgbb key, the Drive refresh token) are stored in the system keyring and never in settings or exports; without a keyring they fall back to a user-only file with a visible notice and an explicit "Move to keyring" action | SV-07 | M2 | improvement over MacShot (DEV-33); §8.17 S8 |

### 8.14 Settings

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-SET-1 | Settings window 620×520, fixed size, seven icon tabs (General, Capture, Shortcuts, Tools, Recording, Uploads, About), 180 pt label column, section headers, toggle grids, footer credit link | ST-01 | M1 (four tabs); M2 | Linux controls use a style within the geometry-parity gate (DEV-38) |
| FR-SET-2 | Every control, conditional state and default of MacShot's 219-key catalogue, with "unset" modelled separately from the effective default and a typed registry that generates defaults, bindings and import validation | ST-02 | M1 subset → M5 | Recording-tab lines are `rec`; update lines are `release` |
| FR-SET-3 | Menu-order editor, theme presets with colour wells, settings backup with a relaunch prompt | ST-05 | M1; M2 (backup) | — |
| FR-SET-4 | Desktop Integration page (Linux): each capability with status, reason and fix; hotkey registration and bound triggers; the managed Hyprland bindings file; on Omarchy, an optional PATH shim so the desktop's screenshot command launches AriadShot; on-device models; the opt-in input helper, explained as a knowing choice | SH-22, SH-33 | M2 | substitute for MacShot's permission onboarding (DEV-30) |

### 8.15 Localisation

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-I18N-1 | 40 UI languages plus "System", converted from MacShot's catalogues (~858 keys) with stable IDs and positional arguments; runtime language switching without restart; right-to-left mirroring (§9.8) | SH-27 | M1 English only (all strings translatable); M2 all 40 | — |

### 8.16 Distribution and release

| ID | Requirement | Rows | Milestone | Platform notes |
| :--- | :--- | :--- | :--- | :--- |
| FR-REL-1 | Linux packages: AUR `-git` from M1 (system Qt, FFmpeg, PipeWire, layer-shell-qt); an AUR release package, an AppImage and a portal-only Flatpak at M5. App ID `io.github.bavanchun.AriadShot` | ST-02 (`release`), SH-25, SH-26 | M1; M5 | no in-app updater (DEV-36) |
| FR-REL-2 | macOS: an `.app` with Qt and FFmpeg frameworks, ad-hoc signed inside out, **not notarized**, in a DMG with MacShot's installer geometry and AriadShot's own art; Sparkle 2 updates with EdDSA signatures and a beta channel; documented, tested first-open steps for Gatekeeper; SHA-256 checksums published with every release (PRD) | ST-02 (`release`), SH-26 | M5 (ad-hoc builds from M2) | MacShot ships Developer-ID-signed, notarized builds and an official Homebrew cask (DEV-40, ADR 0002) |
| FR-REL-3 | Every package ships the GPL text, `PROVENANCE.md` and third-party notices; bundles (AppImage, DMG) carry a link to the complete corresponding source, keep Qt and FFmpeg dynamically linked, and record the exact FFmpeg configuration | — (release criteria) | M5 | — |

### 8.17 Acceptance scenarios for AriadShot-defined behaviour

- **S1 Capture queue without macOS permissions (FR-APP-3).** Given the capability registry is still probing, when a
  capture URL, CLI command or hotkey arrives, then it is held for at most 2 s and run in order once capture works.
  Given a portal consent dialog is open, later requests wait. Given capture is denied or unavailable, then held requests
  are dropped with the pill "Capture isn't available: <reason>" and the CLI exits non-zero with the same text; nothing
  replays later. Non-capture actions (settings, history, open, edit, stop-recording) always run immediately.
- **S2 Unavailable action (FR-GEN-3).** Given an M1 build, `ariadshot://record` shows "“record” isn't available in this
  version of AriadShot", the CLI prints it and exits non-zero, and no Record item exists in any menu or tab.
- **S3 Shortcut recorder on Wayland (FR-KEY-2).** Given the portal backend, when the user records a chord, then
  AriadShot calls the portal with it as the preferred trigger and the row shows the portal's trigger description, or
  "(requested: …)" while unset or different. Given Hyprland, then "Apply in Hyprland" writes only the managed file
  `~/.config/hypr/ariadshot-bindings.lua` and shows the single `require` line to add; AriadShot never edits the user's
  own files. Given the bound chord cannot be read back, the row says "Bound in Hyprland config (chord not readable)".
- **S4 Tab snap cycle (FR-OVL-4).** Given Hyprland, Sway or X11, Tab cycles window → off → window. Given KDE or GNOME
  Wayland, only "off" exists, Tab does nothing and the card shows "Snap mode: OFF" without "(Tab to switch)". A stored
  element mode is coerced at load to the next available mode without rewriting the stored value.
- **S5 IME fallback (FR-ANN-6).** Given a named IME and compositor combination in which preedit does not arrive within
  1 s of composing keys, then text entry for that combination uses a pop-up editor anchored at the text box, falling
  back to the editor window; Enter, click-outside, Esc and scoped undo behave as in place; the committed annotation is
  identical; the Desktop Integration page names the combination.
- **S6 Self-exclusion on Linux (FR-REC-10).** Given a region recording, the outline and HUD are drawn outside the
  region. Given a full-screen recording and a second output, the HUD moves there; with one output it is hidden, the tray
  and the Record hotkey stop the recording, and a one-time notice explains it. Tested on every recording backend; the
  compositor's `no_screen_share` rule is tested but not relied on.
- **S7 Model download (FR-ML-5).** Given a missing Linux model, when the user first invokes Remove Background,
  Vietnamese OCR, offline Translate or captions, then a consent sheet appears and nothing downloads until Download is
  pressed; cancel deletes partial data; a network error or checksum mismatch shows "Couldn't download <model>:
  <reason>" and leaves the feature disabled with a retry link.
- **S8 Credentials without a keyring (FR-NET-2).** Given no secret service answers, then credentials are written to a
  user-only file (0600, directory 0700) excluded from exports and backups, and the Uploads tab shows "Credentials are
  stored in a file readable by your user account (no keyring found)" with a link to the Desktop Integration page.
- **S9 macOS first open (FR-REL-2).** Given a DMG downloaded on macOS 14 and on macOS 26, when the user follows the
  documented first-open steps, then AriadShot launches, and a later Sparkle update installs and relaunches without
  repeating the Gatekeeper steps. Whether permissions survive the update is measured and documented (RK-19).

## 9. Non-functional requirements

### 9.1 Performance budgets

Budgets are target thresholds, **(H)** until G8. Topologies: **P1** a 1920×1200 panel plus a portrait 1080×1920
display at scale 1; **P2** the panel at fractional scale 1.25; **P3** one 3840×2160 output at scale 1 and 2. Every run
records p50/p95 frame times, RSS, CPU pixel copies per frame and dropped frames.

| Budget | Workload | Target |
| :--- | :--- | :--- |
| Hotkey → pointer output frozen and interactive | W1: warm daemon, P1, 20 own windows hidden first | ≤ 100 ms p50, ≤ 150 ms p95 (MacShot's best case on macOS 26: < 400 ms) |
| Hotkey → all outputs frozen | W1 | ≤ 250 ms p95 |
| Hotkey → overlay on GNOME | W1 on GNOME 48+ | measured and documented; no fixed promise |
| Hotkey → overlay on macOS | W1 on the G7 Mac | ≤ MacShot on the same Mac, measured side by side |
| Annotation drag or selection resize | W2: 50 mixed annotations on P1, P2, P3 | ≤ 16.7 ms p95 (P1 is a hard M1 condition) |
| Copy or confirm → image on clipboard | W3: 1920×1200 PNG | ≤ 150 ms p95, encoding off the UI thread |
| Idle daemon | W0: 10 min idle after 20 captures | ≤ 100 MB RSS, 0% CPU, no overlay buffers retained |
| 1080p60 recording | W4: 10 min scrolling browser page, hardware encode | ≤ 15% of one core, 0 dropped frames, ≤ 1 CPU pixel copy per frame |
| Studio preview | W5: 1080p take, every look enabled | 60 fps p95 |
| Linux package size | AUR, system Qt and FFmpeg, no optional models | ≤ 40 MB installed |
| Core model bundle (Linux) | default catalogue | ≤ 40 MB; larger models on demand |
| macOS app bundle | DMG with Qt and FFmpeg frameworks | ≤ 160 MB |

### 9.2 Interaction timing

Every timed MacShot behaviour matches within ±1 frame at 60 Hz, measured by frame-timed capture: thumbnail slide-in
0.30 s and 5 s auto-dismiss paused on hover; history slide 0.12 s; toast 0.30/0.35 s; long-press 0.3 s; beautify
anchor animation (60 Hz, +0.08 per tick, ease-out); countdown ring; click ring 0.3 s.

### 9.3 Rendering fidelity

- **Still contract.** Every still is rendered once into a canonical image (premultiplied ARGB32, sRGB, the captured
  pixel size, device pixel ratio = capture scale) from the model, edit state, source pixels and a fixed font set
  (bundled Inter and Noto Color Emoji on Linux, system fonts on macOS; hinting off). Every surface — overlay, editor,
  thumbnail, pin, history — displays that image and never re-renders annotations. PNG export and the clipboard are
  byte-identical to the canonical image after un-premultiplication; presentation is exact at integer scale and within
  ΔE00 ≤ 1 on ≥ 99.9% of pixels at fractional scale; lossy formats meet PSNR/SSIM thresholds set at G3.
- **Studio contract.** Preview and export render the same scene snapshot with one shader set in a linear working
  space: preview vs pre-encode ΔE00 ≤ 1 on ≥ 99.9% of pixels; decoded export ≥ 38 dB PSNR and ≥ 0.97 SSIM (H, set at
  G5); on Vulkan, OpenGL fallback and Metal.
- **Corpus.** A MacShot reference corpus is captured on a Mac from a build of `b4d4f3a` and kept in a separate public
  repository; every chrome state is shot at fixed sizes. Thresholds are SM-3. Geometry and colour are exact classes;
  font raster, emoji glyphs and Apple preset looks may differ on Linux, each through a registered mask. Standard-control
  windows (Settings, OCR, dialogs, audio merge) pass a geometry-parity check; icons pass a per-icon approval sheet.

### 9.4 Reliability and robustness

- No crash in 24 h of scripted capture-and-annotate cycles under AddressSanitizer and UndefinedBehaviorSanitizer.
- History survives `kill -9` at any point with zero lost committed entries.
- Recordings: in ≥ 95% of ≥ 200 `kill -9` trials at random points of 10-minute takes, the file plays up to the last
  synced fragment in two independent demuxers.
- Every parser of untrusted input (clipboard HTML and RTF, images, settings imports, project, history and telemetry
  files) passes a 24-hour fuzzing run before its milestone exits.
- Failures surface through MacShot's surfaces (error pill, dialog, toast); nothing fails silently.

### 9.5 Privacy

- **No telemetry**: no analytics, crash reporters, beacons or remote logging (NG-3).
- **Network use is limited to:** uploads and translations the user starts; update checks as configured (MacShot checks
  daily by default); model downloads after consent (Linux); and nothing else. Translation through the default Google
  endpoint sends the recognised text to Google, which the documentation states. The offline build removes uploads and
  cloud code and model downloads, and keeps update checks.
- **On-device processing** for OCR, QR, redaction, background removal, face detection, captions ("nothing is
  uploaded"), encoding and rendering.
- **Data stays local and bounded:** settings in `$XDG_CONFIG_HOME/ariadshot/`, history and models in
  `$XDG_DATA_HOME/ariadshot/` (macOS: `~/Library/Application Support/AriadShot/`); history size is user-controlled; the
  daemon holds no pixels while idle.
- **Logs** never contain pixels, clipboard contents, secrets, tokens or file contents; paths only at debug level.
- **Input monitoring** on Wayland requires the opt-in helper, which the user installs knowingly; nothing captures input
  passively.

### 9.6 Security

- Untrusted inputs are bounded by MacShot's limits (16,384 px side, 128 Mi px, 128 MiB images, 4 MiB RTF, 2 MiB HTML,
  2 MiB settings import) and fuzzed; code is built with sanitizers in CI and hardened in release (`_FORTIFY_SOURCE=3`,
  stack protector, standard-library assertions, PIE, full RELRO).
- Credentials in the keyring (FR-NET-2); secrets never in the repository, logs, CI output or exports.
- IPC socket in a 0700 runtime directory; URL-scheme and CLI inputs validated; files written atomically; no shell
  interpolation of user data.
- Model downloads verified by SHA-256 from the catalogue; macOS updates verified by EdDSA.
- Builds fetch nothing at configure, build or test time; dependencies come from system packages or vendored sources
  with recorded origin.
- Vulnerabilities are reported through GitHub Private Vulnerability Reporting (`SECURITY.md`); responses are best
  effort, with no promised response time.

### 9.7 Accessibility

- Every custom-drawn chrome view (toolbars, options rows, popovers, resolution box, helper cards, HUDs) exposes an
  accessible name, role and state; standard widgets keep their accessible names. Target: 100% of chrome views in every
  corpus state, checked by a test that walks the accessibility tree (PRD).
- Keyboard reachability is at least MacShot's: single keys, Tab, the Esc waterfall, arrow nudging, menus.
- Screen-reader smoke tests with Orca (Linux) and VoiceOver (macOS) cover Settings, the OCR window, the editor and the
  history panel at M5.
- Visual changes made for accessibility that MacShot does not make are registered deviations (fidelity principle).

### 9.8 Internationalisation

- 40 languages plus "System": ar, bg, bn, ca, cs, da, de, el, en, es, fa, fi, fil, fr, he, hi, hr, hu, id, it, ja, ko,
  ms, nb, nl, pl, pt, pt-BR, ro, ru, sk, sr, sv, ta, th, tr, uk, vi, zh-Hans, zh-Hant (MacShot's 40 `.lproj` catalogues
  at `b4d4f3a`, excluding `Base`).
- "System" resolves the preferred languages with script tags; switching applies at runtime without restart.
- Right-to-left mirroring for Arabic, Persian and Hebrew.
- Every user-visible string is translatable and uses positional arguments; no text is baked into images; shortcuts
  match layout-aware characters.
- At M2, 100% of MacShot-derived keys are present in all 40 languages; AriadShot-only strings follow Q3.
- IME: Vietnamese Telex (fcitx5 Unikey or Bamboo) and one CJK engine commit correctly in overlay text with the
  candidate window ≤ 50 px from the caret, on Linux and inside the macOS non-activating panel; otherwise S5 applies.

### 9.9 Platform tiers and compatibility

| Tier | Platforms | Promise | Evidence |
| :--- | :--- | :--- | :--- |
| Reference | Hyprland (tested on 0.56.2, Omarchy) | full parity, measured each milestone | host and nested headless Hyprland |
| Reference target, contingent | macOS 14+ | same bar, ≤ 1 milestone behind; without G7 only "builds, launches, captures, copies" | physical macOS 26 Mac and a macOS 14 VM; hosted macOS CI on every PR |
| Supported, contingent | Sway and other wlroots compositors, KDE Plasma 6, X11 (i3, Xfce, KDE X11, GNOME Xorg) | full parity except the per-desktop differences, claimed only for what CI measures | headless Sway, nested KWin, Xvfb/Xephyr |
| Best effort | GNOME Shell on Wayland (48+), Flatpak builds | capture, annotate, editor, history and recording through portals; every gap registered | GNOME 48+ VM (M5) |
| Not supported | Windows, macOS < 14, mobile, web | — | — |

Software floors: Qt ≥ 6.8 (the Qt minor is pinned per release); macOS 14 is claimed only while it is tested; it
narrows to the oldest tested version otherwise.

## 10. Constraints

| ID | Constraint | Consequence |
| :--- | :--- | :--- |
| C-1 | **GPL-3.0-only.** MacShot grants GPLv3 without "or later" (`LICENSE@b4d4f3a`; `README.md:211-213@b4d4f3a`) | Every AriadShot file carries `SPDX-License-Identifier: GPL-3.0-only`; MacShot-derived files add "sw33tLie and MacShot contributors" and a provenance line; `PROVENANCE.md` lists every reused asset (catalogues, tables, ported algorithms, constants). Dependencies must be GPL-3.0-compatible (Qt LGPL modules, Qt NetworkAuth GPL-3.0, FFmpeg per package) |
| C-2 | **No paid accounts or services.** | No Apple Developer ID, so no notarization and no Mac App Store; macOS builds are ad-hoc signed (DEV-40, ADR 0002). Free GitHub hosting and hosted runners; only free provider credentials (AriadShot's own imgbb key and Google Cloud OAuth client, or the user's) |
| C-3 | **No telemetry.** | §9.5; success metrics come from repository artifacts (§5) |
| C-4 | **Pinned baseline** MacShot `b4d4f3a` | Parity numbers refer to that commit until a deliberate re-baseline (Q10) |
| C-5 | **Binding stack** (ADR 0001): C++20, Qt 6 Widgets, QPainter canonical renderer, QRhi + FFmpeg studio, AriadShot's own Wayland connection plus portals and PipeWire, Objective-C++/Swift shims on macOS, ONNX Runtime, zxing-cpp, CTranslate2 and whisper.cpp on Linux | Reopened only by the three triggers in ADR 0001 (still-contract failure specific to QPainter; studio failure on Vulkan and Metal with a passing wgpu spike; fuzzing that outpaces fixes) |
| C-6 | **Wayland security model**: no passive input capture, no client positioning of normal windows, no window list on GNOME, cooperative clipboard | Capability-driven degradation (FR-GEN-4) and deviation entries |
| C-7 | **The user's configuration is respected** | No assumed chords; no edits to compositor configuration without an explicit action; managed files only |
| C-8 | **No MacShot brand or credentials**; no Apple fonts, symbols or emoji on Linux | DEV-01, DEV-02 to DEV-04, DEV-31 |
| C-9 | **Permissive model weights** | The weights licence (not only code) is checked; CC BY-NC (for example RMBG-2.0, NLLB-200) and AGPL (YOLO) models are excluded; unverified licences stay out of the default catalogue |
| C-10 | **Reproducible, network-free builds** | Configure, build and test download nothing; FFmpeg and codec choices per package are recorded |
| C-11 | **Public repository hygiene** | No secrets, personal data or private paths in the repository |
| C-12 | **One maintainer working with AI coding agents** | The maintainer approves every merge, release and outward-facing action; PRs stay small and ledger-scoped |

## 11. Risks

| ID | Risk | Likelihood / impact | Mitigation | Retired at |
| :--- | :--- | :--- | :--- | :--- |
| RK-1 | IME (fcitx5 Telex, CJK) fails in overlay text | Medium / High | G1 step 4 tests two input paths; S5 fallback, disclosed | M0 (G1) |
| RK-2 | Qt Widgets misbehave on layer-shell (keyboard hand-over, popups, latency, rotation) | Medium / High | G1 outcomes: Qt Quick host (1–2 weeks) or hand-written client (4–6 weeks) | M0 (G1) |
| RK-3 | Hotkey-to-overlay latency misses 150 ms p95 | Low–Medium / High | pointer output first, pre-created surfaces, dormant-mapped variant, harness from day one | M0 (G1, G8) |
| RK-4 | CPU canvas misses the frame budget at 4K or when zooming | Medium / Medium | damage-only repaint, raster caches, GPU texture presentation | M0–M1 |
| RK-5 | Click and key telemetry unavailable on Wayland | Certain / Medium | opt-in helper; dependent options disabled with a reason (DEV-24) | M3 |
| RK-6 | GNOME overlay parity impossible with public protocols | Certain / Medium | fullscreen fallback, measurement in a VM, disclosure (DEV-26) | M5 |
| RK-7 | QRhi changes between Qt minors | Low / Medium | compositor behind an interface; Qt minor pinned per release | M4 |
| RK-8 | Apple-only looks cannot be reproduced on Linux | Certain / Low | fitted LUTs, icon approval sheet, registered substitutions | M2 |
| RK-9 | KWin restricts image-copy capture | Medium / Low | runtime probe; ScreenCast with restore tokens | M2 |
| RK-10 | C++ memory-safety defects in a long-running daemon | Medium / High | sanitizers, fuzzing gates, hardening; parsers move to a Rust library if fuzzing keeps finding defects | continuous |
| RK-11 | MacShot keeps evolving | Certain / Medium | pinned baseline; upstream review per release | continuous |
| RK-12 | Upload providers need AriadShot's own keys | Certain / Low | "not configured" state; user keys | M2 |
| RK-13 | Two Wayland connections stall each other | Low / Medium | dedicated thread; G4 stress test | M0 (G4) |
| RK-14 | No Mac, or macOS 14 cannot be tested | Medium / High for macOS | G7 with a VM runner; else "builds only" and a narrower minimum | M0 (G7) |
| RK-15 | Recordings cannot exclude AriadShot's own surfaces on Wayland | Certain / Medium | S6 strategy (DEV-22, DEV-23) | M3 |
| RK-16 | A licence obligation is missed (weights, FFmpeg flags, notices, source offer, codec patents) | Medium / High | provenance ledger, model catalogue, per-package checklist, codec review gate | M5 |
| RK-17 | The Linux recording path drops frames or costs too much CPU | Medium / Medium | G6 measures both candidates | M0 (G6) |
| RK-18 | Un-notarized macOS builds meet Gatekeeper friction and user distrust | Certain / Medium | tested first-open steps (S9), published checksums (PRD), source-build instructions; registered as DEV-40 | M5 |
| RK-19 | macOS privacy permissions (Screen Recording, Accessibility, Input Monitoring, microphone, camera) may reset on every update of an ad-hoc-signed app, because the code identity changes; the G7 permission experiment measures it | Likely / High for macOS users | measure at G7; evaluate a stable free self-signed signing identity (Q1); onboarding explains re-granting | M0 (G7) → M5 |
| RK-20 | The official Homebrew tap excludes AriadShot: casks that fail Gatekeeper checks are disabled there from 2026-09-01 | Certain / Low | DMG and Sparkle remain the primary channel; an own tap is possible (Q2) | M5 |
| RK-21 | Per-PR macOS 14 runtime coverage ends when the hosted image retires (2026-11-02) | Certain / Medium | macOS 14 VM runner (G7); the claim is marked gated until it exists | M0 |
| RK-22 | Maintainer review becomes the bottleneck for agent-written PRs | Medium / Medium | 500-line PR limit, cross-model agent review before the maintainer, ledger-scoped slices | continuous |
| RK-23 | AriadShot-only strings lack translations in 39 languages | Likely / Low | decision Q3; English fallback is visible, not silent | M2 |

## 12. Release criteria

### 12.1 Every milestone

1. Every ledger line of the milestone is `verified` on the platforms of §7.1, or a registered deviation (SM-1, SM-11).
2. The four milestone artifacts are generated from `parity/` and published in the release notes.
3. The milestone's performance budgets are met, or revised in ADR 0001 with measured numbers — never silently.
4. The robustness gates that apply to the milestone's features pass (§9.4).
5. CI is green on every required Linux and macOS job (incl. sanitizers, the Qt 6.8 floor and macOS 14, hosted or on
   the VM runner), and the maintainer's Mac checklist for the milestone's macOS scope has passed.
6. No open issue labelled crash, data loss, security or privacy.

### 12.2 Per milestone

| Milestone | Release | Additional exit criteria |
| :--- | :--- | :--- |
| M0 | none | each gate G1–G8 has an outcome recorded as an ADR (host chosen, recording source chosen, every budget kept or revised); the MacShot corpus exists for the M1 atomic set or its lines are marked reduced confidence |
| M1 | `v0.1.0` | M1 atomic set 100% verified on Hyprland; W2 ≤ 16.7 ms p95 on P1 (a hard condition); the other M1 budgets (W0, W1, W3) met or revised per §12.1; history kill test and 24 h ASan/UBSan run pass; AUR `-git` package; SM-14 met; macOS builds, launches, captures and copies |
| M2 | `v0.2.0` | `shot` lines per §7.1; 40 languages; network-audit test (SM-12); 24 h fuzzing of every parser present; macOS feature gates 1–4; ad-hoc-signed macOS build |
| M3 | `v0.3.0` | `rec` lines; W4 budget; 200-trial recording kill test; self-exclusion tested on every recording backend |
| M4 | `v0.4.0` | `studio` lines; W5 budget; studio contract on Vulkan, OpenGL fallback and Metal |
| M5 | `v1.0.0` | §12.3 and the M5 row of §7.1 |

### 12.3 Release 1.0 gates

- **Licence:** `reuse lint` clean; `PROVENANCE.md` complete; third-party notices in every package; a link to the
  complete corresponding source in the AppImage and DMG; FFmpeg configuration recorded; a codec patent and distribution
  review (H.264, HEVC, AAC, AV1) for the AppImage and DMG.
- **Distribution:** AUR, AppImage, Flatpak and the ad-hoc-signed DMG with a Sparkle EdDSA appcast and checksums (PRD);
  S9 passes on macOS 14 and 26; update checks and the offline build verified.
- **Quality:** accessibility checks of §9.7; every kept budget met on P1–P3; all robustness gates.
- **Documentation:** user guide, Desktop Integration guide per desktop, macOS first-open guide, privacy statement,
  deviation register rendered for users.
- **Upstream:** MacShot changes after `b4d4f3a` reviewed and either scheduled or recorded as out of scope.

## 13. Open questions

| ID | Question | Owner | Blocking? |
| :--- | :--- | :--- | :--- |
| Q1 | Sign macOS builds with a stable free self-signed identity instead of pure ad-hoc signing, if G7 shows permissions reset on update (RK-19)? | maintainer + engineering | before the first macOS release |
| Q2 | Should the project publish its own Homebrew tap at M5? The official Homebrew cask is excluded, because casks that fail Gatekeeper checks are disabled in the official tap from 2026-09-01; third-party taps may still ship them | maintainer | no (M5) |
| Q3 | How are AriadShot-only strings (Desktop Integration, model consent, Linux substitutes) translated into 39 languages: reviewed machine translation, community, or visible English fallback? | maintainer | before M2 |
| Q4 | Are Intel Macs supported (x86_64 or universal builds), or Apple Silicon only? | maintainer + engineering | before the M2 macOS gate |
| Q5 | Which Linux CPU architectures beyond x86-64 are built and tested? | maintainer | no (M5) |
| Q6 | Creating AriadShot's own free imgbb key and Google Cloud OAuth client, and whether Google's app-verification requirements for Drive access are acceptable | maintainer + legal | before Drive and imgbb ship (M2) |
| Q7 | Is the weights licence of the community Vietnamese OCR model permissive? Until confirmed, Vietnamese OCR uses Tesseract's `vie` data | legal + engineering | no (M2) |
| Q8 | Where does Omarchy/Hyprland expose the current wallpaper? Otherwise the Studio wallpaper background stays hidden | engineering | no (M4) |
| Q9 | Does Hyprland's `no_screen_share` rule affect layer surfaces and image-copy capture? | engineering | no (M3) |
| Q10 | Re-baseline parity to a newer MacShot commit before 1.0? | maintainer | no (M5) |
| Q11 | Post-1.0: shipping the GNOME Shell extension evaluated at M5, an AT-SPI element-snapping experiment, net-new features | maintainer | no |
| Q12 | Post-1.0: import of MacShot settings exports | maintainer | no |

## 14. Glossary

| Term | Meaning |
| :--- | :--- |
| Applicable line | A ledger line that counts toward a platform's parity: excludes MAC-ONLY lines on Linux and lines outside the tier promise |
| BACKEND / PORT / WL-HARD / MAC-ONLY | Row tags: needs a platform backend / portable as-is / hard on Wayland, degradation must be shown / macOS concept with a Linux substitute or omission |
| Canonical image | The single rendered image of a still that every surface displays and every export encodes |
| Capability registry | Component that records what the running desktop can do and drives FR-GEN-4 |
| CIEDE2000 (ΔE00) | Perceptual colour-difference formula used by the golden-image comparator |
| Corpus | Reference screenshots and exports produced by MacShot built from `b4d4f3a` |
| Data-control | Wayland protocol that lets a background client own the clipboard (`ext-data-control`) |
| Deviation register | The list of intentional differences from MacShot (`DEV-nn`), each with MacShot behaviour, AriadShot behaviour, reason and platforms; parity is always reported against it |
| Desktop Integration page | Linux Settings page that shows capabilities, reasons and fixes; replaces MacShot's permission onboarding |
| Domain | `shot`, `rec`, `studio` or `release`; milestones gate on domains |
| G1–G8 | M0 go/no-go gates (§7.2) |
| Gatekeeper / notarization / ad-hoc signing | macOS launch checks; Apple's paid-account malware scan; a signature without a certificate identity |
| Image-copy capture | Wayland protocol `ext-image-copy-capture-v1` for capturing outputs and windows |
| IME / preedit | Input method (for example fcitx5 with Telex) and its uncommitted composing text |
| Layer-shell | Wayland protocol for overlay-class surfaces (bars, overlays) above normal windows |
| Ledger line | One atomic, observable parity item with its MacShot source, domain, milestone, per-platform status and tests |
| Opt-in input helper | A Linux component the user installs knowingly to provide click and key telemetry on Wayland |
| P1–P3 / W0–W5 | Display topologies and benchmark workloads of §9.1 |
| Portal | `xdg-desktop-portal` D-Bus APIs (Screenshot, ScreenCast, GlobalShortcuts, OpenURI, FileChooser) |
| Row | One item of the parity digest in ADR 0001 (`SH-01` … `ST-07`) |
| SNI | StatusNotifierItem, the Linux tray protocol |
| Sparkle / EdDSA | macOS update framework and the signature scheme that authenticates updates |
| Surface host (H1, H2, H3) | Adapter that shows canonical images in an overlay-class surface: Qt Widgets, Qt Quick, or a hand-written Wayland client |
| Trace parity | Replaying the same scripted input on MacShot and AriadShot and comparing the resulting models field by field |
| Verified / verified-source | Ledger statuses: full evidence class met / tests assert the MacShot value but the evidence class is pending |

## Appendix A. Parity-row coverage index

All 175 rows are in scope. Column three lists every deviation (`DEV-01` to `DEV-47`) against the rows it affects, with
platforms (A all, L Linux, W Wayland, K KDE, G GNOME, M macOS); DEV-27 (the macOS 14 minimum) covers every macOS row.

| Family | Rows → requirement | Deviations (platforms) |
| :--- | :--- | :--- |
| SH (34) | 01 APP-1 · 02 APP-2 · 03 APP-3 · 04 APP-4 · 05, 06 APP-5 · 07 APP-6 · 08, 09 APP-7 · 10 KEY-1, KEY-2 · 11 KEY-3 · 12 KEY-4 · 13 APP-8 · 14 APP-9 · 15, 16 APP-10 · 17, 18, 21 APP-11 · 19 ML-1 · 20 ML-2 · 22 APP-12, SET-4 · 23 APP-13 · 24 APP-2 · 25 APP-14 · 26 APP-15 · 27 I18N-1 · 28 APP-16 · 29 APP-17 · 30 APP-18 · 31 APP-19 · 32 APP-8 · 33 ML-5, SET-4 · 34 APP-20 | SH-04, SH-30 DEV-46 (A) · SH-05, SH-06 DEV-35 (L) · SH-07 DEV-01 (A), DEV-26 (G) · SH-10 DEV-13 (L), DEV-14 (W) · SH-14 DEV-11 (A) · SH-16 DEV-26 (G) · SH-19 DEV-06 (L) · SH-22 DEV-30 (L) · SH-23 DEV-28 (A) · SH-25 DEV-32 (L) · SH-26 DEV-36 (L), DEV-40 (M) · SH-28 DEV-01 (A) · SH-29 DEV-47 (L) · SH-31 DEV-12 (L) · SH-33 DEV-20, DEV-32 (L) |
| OV (36) | 01 OVL-1 · 02, 03, 24, 36 OVL-2 · 04–06 OVL-3 · 07–09 OVL-4 · 10–13 OVL-5 · 14, 19 OVL-6 · 15, 16 OVL-7 · 17, 18 OVL-8 · 20 OVL-9 · 21 OVL-10 · 22, 23 OVL-11 · 25, 26 OVL-12 · 27 OVL-13 · 28 OVL-14 · 29 OVL-15 · 30, 31 OVL-17 · 32–34 OVL-16 · 35 OVL-18 | OV-01 DEV-26 (G) · OV-04, OV-07 DEV-15 (L) · OV-08 DEV-17 (K, G) · OV-09 DEV-16 (L) · OV-23 DEV-34 (A) · OV-28 DEV-18 (L) · OV-33 DEV-08 (A) |
| TB (15) | 01, 02 TBR-1 · 03–06 TBR-2 · 07–09 TBR-3 · 10 TBR-4 · 11, 12 TBR-5 · 13 TBR-6 · 14, 15 TBR-7 | TB-05 DEV-02 (L) · TB-14 DEV-03, DEV-04 (L) |
| AN (24) | 01, 02 ANN-1 · 03 ANN-2 · 04 ANN-3 · 05, 06a ANN-4 · 06b ANN-5 · 07 ANN-6 · 08 ANN-7 · 09a, 09b ANN-8 · 10 ANN-9, ML-4 · 11–14 ANN-10 · 15 ANN-11 · 16 ANN-12 · 17, 17b ANN-13 · 18, 19 ANN-14 · 20, 21 ANN-15 | AN-07 DEV-07, DEV-39 (A), DEV-21 (W) · AN-10 DEV-45 (A), DEV-06 (L) · AN-14 DEV-04 (L) · AN-18 DEV-28 (A) · AN-20 DEV-09 (A) · AN-21 DEV-03 (L) |
| CR (24) | 01 CAP-1 · 02 CAP-2 · 03 CAP-3 · 04 CAP-4 · 05 CAP-5 · 06 CAP-6 · 07, 08 REC-1 · 09 REC-2 · 10 REC-3 · 11 REC-4 · 12 REC-5 · 13 REC-6 · 14 STU-6 · 15, 16 REC-7 · 17–19 REC-8 · 20, 21 REC-9 · 22 STU-7 · 23 REC-10 · 24 REC-11 | CR-05 DEV-25 (K, G), DEV-41 (W) · CR-08, CR-15 DEV-28 (A) · CR-10 DEV-29 (A) · CR-15, CR-17, CR-18 DEV-24 (W) · CR-17, CR-18, CR-19 DEV-23 (L) · CR-18 DEV-43 (L) · CR-19, CR-20, CR-21 DEV-26 (G) · CR-20, CR-23 DEV-22 (L) · CR-22 DEV-06 (L) · CR-23 DEV-23 (L) |
| VE (15) | 01, 13 STU-1 · 02–05 STU-2 · 06, 07 STU-3 · 08 STU-4 · 09–12 STU-5 · 14 STU-6 · 15 STU-7 | VE-01 DEV-28 (A) · VE-02 DEV-19 (L) · VE-03, VE-07 DEV-24 (W) · VE-05 DEV-43 (L) · VE-14 DEV-42 (A) |
| ED (12) | 01–04 WIN-1 · 05 WIN-2 · 06 WIN-3 · 07 WIN-4 · 08, 09 WIN-5 · 10 WIN-6 · 11, 12 WIN-7; 11 also NET-1 | ED-04, ED-06 DEV-18 (L) · ED-05, ED-07, ED-08, ED-11 DEV-26 (G) · ED-10 DEV-38 (L) |
| SV (8) | 01 BTY-1 · 02 BTY-2 · 03 OUT-1 · 04 OUT-2 · 05 OUT-3 · 06 ML-3 · 07 NET-1, NET-2 · 08 KEY-3 | SV-01 DEV-10 (A), DEV-44 (M) · SV-02 DEV-05 (L) · SV-03 DEV-26 (G) · SV-05 DEV-28 (A) · SV-06 DEV-06 (L) · SV-07 DEV-31, DEV-33 (A) |
| ST (7) | 01 SET-1 · 02 SET-2, REL-1, REL-2 · 03 KEY-2 · 04 OUT-2 · 05 SET-3 · 06 APP-1 · 07 ML-3 | ST-01 DEV-38 (L) · ST-02 release lines DEV-40 (M) · ST-03 DEV-14 (W) · ST-07 DEV-37 (L) |
