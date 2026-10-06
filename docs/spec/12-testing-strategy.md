<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 12. Testing Strategy

Part of the [AriadShot technical specification](README.md). Day-to-day testing rules for contributors are in
`docs/dev/testing.md`; this file specifies what the test system must prove and how. Decisions:
[ADR 0001](../adr/0001-architecture-and-stack.md), [ADR 0002](../adr/0002-foundation-and-workflow.md).

## 1. Principles

1. **Parity is measured against MacShot, not against AriadShot.** A test asserts the MacShot value recorded in the
   parity ledger (§5), never an AriadShot constant. A test that compares a default with the constant that defines it
   proves nothing and is rejected in review.
2. **Every behaviour change ships with tests.** Run the narrowest test first, then the `dev` workflow; run `asan`
   before requesting review for code that touches memory, parsing or threads.
3. **Evidence classes.** A ledger line is `verified` only with the evidence its class requires: visual lines need a
   MacShot corpus golden (§4), interaction lines a trace-parity script (§6), defaults lines a defaults test (§7),
   timing lines a frame-timed measurement (§10).
4. **Contracts are tests.** The still and studio rendering contracts ([03](03-rendering-contracts.md)) are acceptance
   tests that run in CI on every commit that touches `render/`, `media/` or a surface host.
5. **Nothing turns green by weakening.** Failing tests are never deleted or loosened to pass. A flaky test is
   quarantined only with an issue and owner approval. Golden images are never created or updated to make a test pass
   (§4.5). Harness regressions fail loudly: a tier-2 or tier-3 test that misses an expected protocol global fails; it
   never skips.
6. **Tests never touch the user's real state**: configuration, history, sockets, tray, shortcuts or clipboard.

## 2. Test tiers

| Tier | Environment | What runs there | Where |
| :-: | :--- | :--- | :--- |
| 0 | `QCoreApplication` | `core` unit tests | every CI job, local |
| 1 | offscreen QPA with bundled fonts and a neutral Qt environment (§3.3) | `render`, `ui` and `windows` tests; goldens; trace-parity replays; rendering-contract tests | every CI job including macOS, local |
| 2 | headless Sway (`WLR_BACKENDS=headless`, `WLR_RENDERER=pixman`, two headless outputs, one with `transform 90`) | layer-shell mapping and keyboard focus, output image-copy capture, screencopy, data-control: the wlroots protocol client code | CI (label `wayland`), local |
| 3 | nested Hyprland (a window in the live session, headless outputs created inside the nested instance) | Hyprland-specific protocols (toplevel export, global shortcuts, layer rules), G1 and G2 acceptance, fractional scale P2 | reference host (label `nested-hyprland`) |
| 4 | live Hyprland session | real portals and outputs, performance budgets (G8), capture of real applications | reference host; every run approved by the owner |
| M | hosted `macos-26` (arm64), the owner's Mac (macOS 26) and a macOS 14 VM on it | tier 1 on every pull request; permissions, `NSPanel`, IME and capture on the Mac | CI plus the Mac checklist (§13) |

Notes:

- Weston and Cage lack layer-shell upstream and are not used for tier 2. Tier 2 is a regression suite for protocol
  client code shared by wlroots compositors; Hyprland acceptance runs only on tiers 3 and 4.
- Every tier-2 and tier-3 test first dumps the registry of its own Wayland connection and asserts the protocol
  inventory it needs.
- X11 (Xvfb/Xephyr) and nested KWin join with their backends (M2 for the M1 atomic set, M5 for their tier promises); a
  GNOME 48+ VM joins at M5.
- Harness scripts record every compositor and application process they start (PID, start time, command line) and stop
  exactly those on exit.
- Tier 2 becomes a required step of the `linux-gcc` CI job once its harness proves itself in the CI container
  [HYPOTHESIS: headless Sway runs in the Arch CI container with the pixman renderer]. Tier-3 and tier-4 results are
  attached to pull requests as run logs.

## 3. Layout, naming, labels and environment

### 3.1 Layout

| Path | Contents |
| :--- | :--- |
| `tests/unit/<module>/tst_<Topic>.cpp` | unit tests per module (Qt Test) |
| `tests/golden/<suite>/` | golden tests and regression goldens `<name>[.<platform>].png` |
| `tests/trace/` | trace-parity scripts and their expected results (from M1) |
| `tests/integration/` | tier-2 and tier-3 tests |
| `tests/fuzz/` | one libFuzzer target per parser of untrusted input, with its seed corpus |
| `tests/cmake/` | build-system negative and positive fixtures (architecture checks) |
| `tests/scripts/` | cases for repository scripts: commit messages, agent guard, include rules, ledger round trip |
| `tests/support/` | shared test library: `ImageCompare`, environment helpers |

### 3.2 Naming and labels

- CTest names are `<module>.<topic>` and describe behaviour. Test names never contain ledger, deviation, gate or
  milestone identifiers; the ledger maps identifiers to test names (§5).
- Labels: `unit`, `golden`, `wayland` (tier 2), `nested-hyprland` (tier 3), `live` (tier 4),
  `gpu` (hardware GPU or VA-API), `tsan-safe` (code without Qt threading, run under TSan). The `dev`, `asan` and
  `release` test presets exclude `wayland`, `nested-hyprland`, `live` and `gpu`; the `tsan` preset runs `tsan-safe`
  only.
- Every test is declared through `ariadshot_add_test(NAME … SOURCES … LIBS … LABELS …)` in `tests/CMakeLists.txt`.

### 3.3 Neutral test environment

`ariadshot_add_test` sets, through CTest `ENVIRONMENT_MODIFICATION`:

- `QT_QPA_PLATFORM=offscreen` for tiers 0–1;
- unset: `QT_QPA_PLATFORMTHEME`, `QT_STYLE_OVERRIDE`, `QT_IM_MODULE`, `QT_PLUGIN_PATH`, `QT_SCALE_FACTOR`,
  `QT_SCREEN_SCALE_FACTORS`, `QT_AUTO_SCREEN_SCALE_FACTOR`, `QT_ENABLE_HIGHDPI_SCALING`, `QT_FONT_DPI`;
- temporary `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_STATE_HOME` and `XDG_RUNTIME_DIR` per test.

Tests call `QStandardPaths::setTestModeEnabled(true)`. Features that own global state (the control socket, shortcut
registrations, the tray, the clipboard selection) honour an instance namespace from their first commit, so tests can
run them without touching the session. Reason for the neutral environment: a desktop platform theme such as `gtk3`
loads fontconfig, Pango and GLib into the test process, which makes goldens host-dependent and produces leak reports
under AddressSanitizer. The sanitizer workflows must pass with such a theme set in the calling shell. Packaging
`check()` steps unset the same variables. On Linux, `render::FontSet` registers bundled fonts explicitly; the offscreen
Qt platform ignores `QT_QPA_FONTDIR`.

## 4. Golden images

### 4.1 Two comparison classes

The comparator is implemented once, in `tests/support/ImageCompare`, and exposed on the command line by
`tools/imgdiff`.

| Class | Used for | Pass rule |
| :--- | :--- | :--- |
| **Exact** | canonical renders, PNG round trips, same-platform regression goldens, presentation at integer scale | equal pixel size and device pixel ratio, and byte-equal pixel data after converting both images to `QImage::Format_RGBA8888` (Qt's un-premultiplication is the single specified rounding rule) |
| **Perceptual** | fractional-scale presentation, AriadShot against the MacShot corpus | per pixel: both alphas below 8/255 → pass; else `|Δα| > 1/255` → fail; else convert the un-premultiplied colours sRGB → linear (IEC 61966-2-1) → XYZ (D65) → CIELAB and compute CIEDE2000; the pixel passes when ΔE00 ≤ T. The suite passes when the passing fraction ≥ P over pixels outside the fixture's declared tolerance masks |

Perceptual thresholds:

| Comparison | T | P | Masks |
| :--- | :-: | :-: | :--- |
| Fractional-scale presentation against the canonical image resampled with the documented filter | 1 | 99.9 % | none |
| macOS output against the MacShot corpus | 2 | 99.5 % | none |
| Linux output against the MacShot corpus | 2 | 99.5 % | font-raster and emoji-glyph regions (DEV-03, DEV-04) and effect-preset looks (DEV-05) only; each mask names its deviation entry. Icons (DEV-02) are judged on the per-icon approval sheet and standard controls (DEV-38) by the geometry-parity gate, never masked |

Lossy exports (JPEG, WebP, AVIF, HEIC) and studio frames are compared by PSNR and SSIM with the thresholds set at G3 and
G5 ([03](03-rendering-contracts.md)).

### 4.2 Linux tolerance classes against the corpus

Geometry and colour are exact (ΔE00 ≤ 2). Font raster, emoji glyphs and the `CIPhotoEffect` preset looks may differ
and are masked. Every allowed difference is a deviation-register entry, so parity reports always show what was masked
and why.

### 4.3 Comparator correctness

`tst_ImageCompare` verifies the CIEDE2000 implementation against the published test data of Sharma, Wu and Dalal
(2005) to 1e-4, plus hand-made cases for the alpha rules and the exact-class conversion.

### 4.4 Failure artifacts

On failure the helper writes `actual.png`, `expected.png`, a failure mask and statistics (passing fraction, maximum
and p99 ΔE00) into the test's output directory. The test fails through `QVERIFY` in the test function itself, not
through `QFAIL` inside a helper, which would only return from the helper.

### 4.5 Golden custody

- **Regression goldens** are produced by running the test with `ARIADSHOT_WRITE_GOLDEN_CANDIDATES=1`, which writes
  candidates into the build directory only. A human-reviewed pull request labelled `golden-update` copies them into
  `tests/golden/` with before, after and difference images; only the owner's approval lets it merge. A golden may carry
  a per-platform variant (`name.<platform>.png`) when rasterisation legitimately differs between x86-64 and arm64.
- **Corpus goldens** come only from the MacShot build on the owner's Mac (§4.6). Agents never create or update golden
  images to make a test pass.

### 4.6 MacShot reference corpus

- Captured on a Mac from a build of MacShot at `b4d4f3a`, starting with the M1 atomic set. Fixture annotation sets,
  beautify styles and effect presets are loaded through MacShot's own history format (a revision folder with
  `annotations.json` and `edit.json`) and exported at 1× and 2×. MacShot's probe harnesses (`scripts/probe-history-editor.sh`
  and `scripts/probe-video-editor.sh` in the MacShot repository) are the preferred drivers.
- Every chrome state is screenshotted at fixed sizes: toolbars, the options row of every tool, popovers, the resolution
  box, helper cards, HUDs, thumbnail, pin, history, the OCR window, the editor, each Settings tab and the studio.
- Stored in the separate public repository `bavanchun/ariadshot-corpus` (images, capture manifests, MacShot build
  information). This keeps large binaries and MacShot's UI renders out of the product repository's history. CI fetches a
  pinned corpus commit as an acquisition step, never during the build. Git LFS is not used.
- If no Mac corpus exists for a line, the corpus falls back to MacShot's published assets and the parity report marks
  that line "reduced confidence".

## 5. Parity ledger

### 5.1 Files and fields

| File | Contents | Written by |
| :--- | :--- | :--- |
| `parity/ledger/<prefix>.jsonl` (`sh`, `ov`, `tb`, `an`, `cr`, `ve`, `ed`, `sv`, `st`) | generated lines | `tools/ledger` only; regenerated, never hand-merged |
| `parity/ledger/manual-<prefix>.jsonl` | hand-written lines for rows without a machine-extractable source | pull requests |
| `parity/status/<prefix>.jsonl` | per-line status, keyed by `id` | the pull request that adds the evidence |
| `parity/deviations.jsonl` | the deviation register | pull requests, reviewed by the owner |

One JSON object per line keeps diffs and merge conflicts line-local and needs no parser beyond `jq` or Python.

Ledger line fields:

| Field | Type | Meaning |
| :--- | :--- | :--- |
| `id` | string | stable line identifier, `<ROW>.<item>` (for example `SH-08.capture-area`) |
| `parent` | string | the parity row (`SH-08`) |
| `domain` | `shot` \| `rec` \| `studio` \| `release` | milestone gating domain, including the overrides of [14 §1](14-gates-and-milestones.md#1-milestone-model) |
| `milestone` | `M1` … `M5` | the milestone that must deliver the line |
| `macshot` | object `{path, line, value}` | MacShot source location at `b4d4f3a` and the observable value |
| `tags` | array | `PORT`, `BACKEND`, `WL-HARD`, `MAC-ONLY` |

Status entry fields: `id`; per platform (`hyprland`, `wlroots`, `kde`, `x11`, `gnome`, `macos`) a `status`, the
`tests` that prove it, and a `deviation` identifier where one applies.

Statuses: `open`, `verified-source` (tests assert the MacShot source value, but the line's full evidence class is not
yet met), `verified`, `deviation`, `not-applicable`. Every `not-applicable` and `deviation` status names a deviation
entry, so parity is always reported with its exceptions.

### 5.2 Generation

`tools/ledger` generates lines directly from MacShot source, whose path it takes as an argument, re-implementing the
extraction of the lists MacShot holds in machine-extractable form:

- the settings key table (every `UserDefaults` key with its type and effective default, keeping "unset" and "default"
  apart);
- the tool-options specifications (`macshot/UI/Toolbar/ToolOptionsRowView.swift`, with toolbar actions in
  `macshot/UI/Toolbar/ToolbarDefinitions.swift`);
- the single-key shortcut table (`macshot/Services/ToolShortcutManager.swift`);
- the hotkey slots (`macshot/Services/HotkeyManager.swift:12`);
- the overlay key registry (`macshot/UI/Overlay/OverlayView.swift`);
- the studio key matrix (`macshot/UI/Editor/VideoEditorWindowController.swift` and
  `macshot/UI/Editor/Video/VideoTimelineView.swift`);
- the per-handler modifier rules (`macshot/UI/Tools/`).

The generator's output is reviewed once against independent hand extraction; mismatches are resolved against source.
A round-trip test in `tests/scripts/` checks that regeneration leaves status files intact and flags status entries
whose `id` disappeared.

### 5.3 Checks

- Each platform's CI job runs a ledger check that fails when a `verified` or `verified-source` entry for that platform
  names no test, or names a test that this job's CTest does not list.
- Status changes to `verified-source` or `verified` happen only in the pull request that adds the passing tests, or in
  a pull request that attaches the run log of an environment CI does not have (nested Hyprland, live host, owner's Mac).
- `tools/ledger report` produces per-platform verified and applicable counts and the deviation list for each milestone
  exit and on demand. Parity per platform = verified lines ÷ applicable lines; "applicable" excludes `MAC-ONLY` lines on
  Linux and lines outside a tier promise, each still with a deviation entry.

### 5.4 Definition of done

**Per ledger line, per platform**, a line is `verified` when:

1. the implementation is merged on `main`;
2. the tests named in the line pass in CI, or in the named environment with its run log attached;
3. the asserted values equal the MacShot value cited in the line;
4. the line's evidence class is met (§1 item 3);
5. any intentional difference has a deviation entry, and the line says so;
6. a reviewer other than the implementer checked the cited source lines;
7. `PROVENANCE.md`, translations, accessibility names and documentation are updated where the line touches them.

**Per slice:** all its lines are at least `verified-source` on Hyprland (items 1–3 and 5–7; item 4 may wait for corpus
images); CI is green on every required job, including macOS; no new sanitizer, clang-tidy or clazy finding; no open
blocking review finding.

**Per milestone:** every line of the milestone is `verified` or a registered deviation; the four milestone artifacts
([14 §1](14-gates-and-milestones.md#1-milestone-model)) are generated from `parity/`; the owner's Mac checklist for the
milestone's macOS scope has passed.

## 6. Behavioural trace parity

Trace parity is the only gate that catches drift in modifier rules, undo grouping and selection behaviour.

- **Scripts.** A trace script is a data file of timed input events in canvas points: pointer down, move and up with
  buttons and modifiers, key presses with modifiers, wheel, and text input. Scripts cover drags with every modifier
  combination, Tab cycles and Esc waterfalls, undo and redo across group IDs, text re-edit, and ⌘D/⌘V offsets.
- **Replay.** AriadShot replays a script through `ui::ViewRoot::dispatch` on the offscreen platform and, for
  host-dependent behaviour, in nested Hyprland. MacShot replays the same script through its own event pipeline on the
  Mac; the MacShot-side driver and its outputs live in the corpus repository.
- **Comparison.** The resulting serialized annotation documents ([06](06-annotation-model-and-formats.md)) and
  selection rectangles are compared field by field after normalising identifiers (UUIDs are mapped by creation order)
  and converting MacShot's format into AriadShot's. A mismatch fails unless the line is a registered deviation, in
  which case the script encodes AriadShot's registered behaviour (for example DEV-07 and DEV-39 undo restoration).
- The exact script schema and the MacShot driver are delivered by the harness lane of M0; this section fixes what they
  must be able to express.

## 7. Defaults parity and census tests

- **Defaults parity.** A test generated from the ledger's settings lines checks every key against MacShot's effective
  default, keeping "unset" and "default" apart. The single-key table and the hotkey table are checked the same way. A
  mismatch fails unless the line carries a deviation (for example DEV-13 for Linux hotkey defaults).
- **Census.** Modelled on MacShot's `macshotTests/AnnotationPersistenceTests.swift`: a census test enumerates every
  persisted field of the annotation, edit-state, settings and video-project models and fails when a field exists
  without a serialization round-trip test, a clone test and a default test. Adding a persisted field therefore forces
  the three tests.

## 8. Rendering-contract tests

| Contract | Tests | First proven | Then |
| :--- | :--- | :--- | :--- |
| Still ([03 §2](03-rendering-contracts.md#2-still-image-contract)) | *golden*: canonical renders against the corpus; *export*: decoded files against canonical images (PNG exact, lossy by PSNR/SSIM); *presentation*: a test-mode readback of each host's buffer, and in the nested compositor an image-copy of the output, against the canonical image (exact at integer scale, perceptual T = 1, P = 99.9 % at fractional scale) | G3: three representative tools, a mesh gradient, the calibrated shadow, text with emoji | every pull request touching `render/` or a host |
| Studio ([03 §3](03-rendering-contracts.md#3-studio-contract)) | for sampled times in fixture projects: preview rendered at export resolution against the export's pre-encode render (ΔE00 ≤ 1 on 99.9 % of pixels); decoded exported frame against the pre-encode render (PSNR ≥ 38 dB, SSIM ≥ 0.97 [HYPOTHESIS, set at G5]) | G5 on Vulkan and the OpenGL fallback; Metal at G7 | every pull request touching `media/studio/` |

## 9. Performance budgets

All budgets are target thresholds and remain [HYPOTHESIS] until gate G8 records a baseline. After M0 each budget is
kept, revised or rejected in an ADR with the measured numbers; a revision is never silent.

**Topology profiles.**

| Profile | Displays |
| :--- | :--- |
| P1 | the reference host as used: a 1920×1200 internal panel plus a second display rotated to 1080×1920, scale 1 |
| P2 | the internal panel at fractional scale 1.25 (nested or headless Hyprland) |
| P3 | a single 3840×2160 output at scale 1 and at scale 2 (headless output) |

Every run records p50 and p95 frame times, resident memory, CPU pixel copies per frame and dropped frames.

| Budget | Workload | Target |
| :--- | :--- | :--- |
| Hotkey → pointer output frozen and interactive | W1: warm daemon, P1, 20 own windows hidden first | ≤ 100 ms p50, ≤ 150 ms p95 (MacShot's best case is under 400 ms on the pointer display) |
| Hotkey → all outputs frozen | W1 | ≤ 250 ms p95 |
| GNOME hotkey → overlay | W1 on GNOME 48+ | measured and documented; no fixed promise |
| macOS hotkey → overlay | W1 on the owner's Mac | ≤ MacShot on the same Mac, measured side by side |
| Annotation drag or selection resize | W2: 50 mixed annotations; P1, P2, P3 | ≤ 16.7 ms p95 (damage-only repaint) |
| Copy or confirm → image on clipboard | W3: 1920×1200 PNG | ≤ 150 ms p95 (encoding off the GUI thread) |
| Idle daemon | W0: 10 minutes idle after 20 captures | ≤ 100 MB resident, 0 % CPU, no overlay buffers retained |
| 1080p60 recording | W4: 10 minutes of a scrolling browser page, VA-API | ≤ 15 % of one core, zero dropped frames, ≤ 1 CPU pixel copy per frame; measured separately for image-copy and PipeWire |
| Studio preview | W5: a 1080p take with every look enabled | 60 fps p95 on Vulkan |
| Linux package size | AUR package with system Qt and FFmpeg, no optional models | ≤ 40 MB installed |
| Core model bundle | Linux default catalogue | ≤ 40 MB; larger models on demand |
| macOS app bundle | ad-hoc-signed DMG with Qt and FFmpeg frameworks (DEV-40) | ≤ 160 MB |

The harness is `tools/bench`; it drives the daemon through the control socket, timestamps with the monotonic clock,
and captures frame timing with image-copy traces in a nested compositor or with a screen recorder on tier 4. Tier-4
runs need the owner's approval, and the final G1, G2 and G8 measurement runs use the P1 topology with its second display
connected; agents ask the owner and wait before those runs.

## 10. UX timing budgets

Every timed MacShot behaviour is checked with frame-timed capture on the reference host, within ±1 frame at 60 Hz:

| Behaviour | MacShot value | Source |
| :--- | :--- | :--- |
| Thumbnail slide-in; auto-dismiss after `thumbnailAutoDismiss` seconds (default 5, 0 disables), paused on hover | 0.3 s; 5 s | `macshot/UI/Windows/FloatingThumbnailController.swift:317`, `:333` |
| History panel slide | 0.12 s | `macshot/UI/Windows/HistoryOverlayController.swift:22` |
| Upload toast slide in / out | 0.3 s / 0.35 s | `macshot/UI/Windows/UploadToastController.swift:80`, `:253` |
| Long-press to move an annotation | 0.3 s | `macshot/UI/Overlay/OverlayView.swift:8574` |
| Beautify toolbar anchor animation | 60 Hz, +0.08 progress per tick (about 0.2 s), ease-out | `macshot/UI/Overlay/OverlayView.swift:3455` |
| Click highlight ring lifetime | 0.3 s | `macshot/UI/Overlay/MouseHighlightOverlay.swift:74` |
| Capture-delay countdown tick | 1 s per digit | `macshot/AppDelegate.swift:1345` |

The ledger lines that own these values carry the exact citations; this table is the list of behaviours that need a
frame-timed measurement.

## 11. Robustness gates

| Gate | Pass condition |
| :--- | :--- |
| Soak | no crash in 24 hours of scripted capture-and-annotate cycles under AddressSanitizer and UndefinedBehaviorSanitizer |
| History durability | history survives `kill -9` at any point with zero lost committed entries ([07](07-storage-history-settings.md)) |
| Recording durability | at least 200 `kill -9` trials at random points of 10-minute takes; in at least 95 % of trials the file is playable up to the last synced fragment, judged by FFmpeg's libavformat and GPAC `MP4Box` (test tools only) ([05](05-recording-and-studio.md)) |
| Fuzzing | every parser of untrusted input passes a 24-hour libFuzzer run: clipboard HTML and RTF, images, settings imports, project, history and telemetry files ([11](11-security-privacy.md)) |

Each gate is run at every milestone exit.

## 12. Sanitizers and static analysis

- `asan` runs the whole offscreen suite under ASan and UBSan. LeakSanitizer suppressions name single allocation
  functions only, each added with a reproduced stack; the suppression file starts empty.
- `tsan` runs tests labelled `tsan-safe`, because the system Qt is not instrumented.
- clang-tidy and clazy analyse tracked translation units under `src/` and `tests/` only, so generated MOC output is never
  analysed ([13 §5](13-build-ci-release.md#5-formatting-linting-and-repository-checks)).

## 13. macOS verification

macOS verification is a routine step, not an occasional one:

- **Every pull request:** the required `macos` job (hosted `macos-26`, arm64, pinned Qt, deployment target 14.0, tier-1
  tests) and, until GitHub retires the image on 2026-11-02, the required `macos-14` job, the only per-pull-request check of the
  macOS 14 runtime.
- **Per slice** that touches `render/`, `ui/`, `hosts/`, `windows/`, `backends/macos/` or packaging, and at every
  milestone exit: the owner runs `scripts/macos/verify.sh` on the Mac against the pull request's head commit (build,
  full test suite, then the slice's manual checklist in `docs/dev/macos-checklist.md`: permission prompts, `NSPanel`
  behaviour, IME, capture, visual spot checks) and pastes the result into the pull request.
- **From G7:** the same script runs inside the macOS 14 VM, registered as a self-hosted runner used only by the
  `macos-vm` workflow on `workflow_dispatch` and a schedule on `main` ([13 §6](13-build-ci-release.md#6-continuous-integration)).
  When the hosted macOS 14 image retires, this scheduled run replaces the per-pull-request macOS 14 check, and the
  parity report states that per-pull-request macOS 14 coverage has ended.
