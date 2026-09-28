<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 05. Recording and Studio

Part of the [AriadShot technical specification](README.md). This file specifies the screen recorder (`rec` domain)
and the studio video editor (`studio` domain): lifecycle, sources and buffer paths, encoding, the writer's states and
crash recovery, telemetry, live overlays, and the studio's editing, playback and export behaviour. Decisions and their
rationale: [ADR 0001](../adr/0001-architecture-and-stack.md).

What this file does **not** define:

- the take folder files, the video project document, the telemetry file and the take status file — their fields,
  versions and decoding rules are in [06](06-annotation-model-and-formats.md); their locations are in
  [07](07-storage-history-settings.md);
- the studio rendering contract (`FrameScene`, pass list, colour space, preview-equals-export tests) — see
  [03 §3](03-rendering-contracts.md);
- the recording setup UI inside the overlay (setup bar TB-09, recording popover OV-31, recording outline OV-30) — see
  [04](04-capture-and-overlay.md).

## 1. Scope

| Area | Parity rows | Module |
| :--- | :--- | :--- |
| Recorder lifecycle, clock, configuration | CR-07, CR-09 | `core/session/`, `media/record/` |
| Sources, buffer paths, cadence | CR-10, CR-11 | `platform::CaptureBackend`, `backends/*`, `media/sources/`, `media/gpu/` |
| Writer, fragments, recovery, failure states | CR-10 to CR-12 | `media/write/` |
| Telemetry, cursor motion | CR-15, CR-16 | `platform::InputTelemetry`, `media/telemetry/`, `core/video/` |
| Live overlays, HUD, tray stop, self-exclusion | CR-17 to CR-21, CR-23 | `ui/` panels on surface hosts, `app/` |
| Finishing a take | CR-13, CR-24, SH-34 (Show Recordings) | `app/`, `media/audio/`, `windows/AudioMergeDialog` |
| Studio editor | VE-01 to VE-15, CR-22, SH-30 (video route) | `core/video/`, `media/studio/`, `windows/StudioWindow` |
| GIF writer | CR-14 | `media/gif/` |

All numeric behaviour below comes from MacShot at `b4d4f3a`; every rule cites its source. Where Linux cannot
reproduce a macOS behaviour, the rule names its deviation (`DEV-nn`) and the capability that decides it
([08 §1](08-platform-integration.md)).

## 2. Recording lifecycle and configuration

### 2.1 States and sessions

The recorder follows MacShot's lifecycle exactly (`macshot/Capture/RecordingLifecycle.swift:5-51`):

```text
idle → preparing → recording ⇄ paused → stopping → idle
```

- Every transition out of `idle` creates a session UUID (`core::RecordingLifecycle`). Every asynchronous callback —
  source frames, audio buffers, encoder completions, watchdogs, camera failures — carries the UUID it was started
  with, and a callback whose UUID is not current is discarded (`macshot/Capture/RecordingLifecycle.swift:18`).
- `media::Recorder::State` mirrors these five states; `stateChanged` is emitted on the GUI thread.
- **Session time** is the monotonic clock minus the total paused duration (`core::RecordingClock`,
  `macshot/Capture/RecordingClock.swift`). The HUD timer, telemetry timestamps, camera timestamps and the writer's
  media time all use it. Media time of a sample = source time − start anchor − paused total.
- The capture gate (SH-13) applies: a recording cannot start while a capture is in progress, and no capture starts
  while a recording is active (`macshot/AppDelegate.swift:430,472-477`).

### 2.2 Entry points and region

| Entry | Behaviour | Source |
| :--- | :--- | :--- |
| Record Area (menu, hotkey, `record` URL/CLI action) | opens the capture overlay in recording mode; the user selects a region and presses Start in the setup bar (TB-09) | `macshot/AppDelegate.swift:1191-1203` |
| Record Screen (menu, hotkey, `record-fullscreen`) | records the whole display; with a capture delay configured, recording starts automatically after the countdown | `macshot/AppDelegate.swift:1205-1218` |
| Stop (HUD, status item, `stop-recording`, Record hotkey where DEV-22 applies) | `Recorder::stop()` | §9 |

- A recording covers one output. `RecordingConfiguration` rejects a region that is not contained in its output and a
  frame rate outside 1–120 fps (`macshot/Capture/RecordingConfiguration.swift:28-56`).
- The region is converted to device pixels at the output's scale and rounded **down to even** width and height,
  which H.264 requires (`macshot/Capture/VideoEncodingSettings.swift:157-162`).
- Countdown (SH-14): when a delay is set, the countdown panel is centred on the selected region and is
  mouse-transparent; Esc cancels it (`macshot/AppDelegate.swift:2723-2760`). The panel is an overlay-class surface
  (role `Countdown`) and is never part of the recording.

### 2.3 Configuration

| Setting | Rule | Source |
| :--- | :--- | :--- |
| Frame rate | `recordingFPS`, default 30; the Settings popup offers 15, 24, 30, 60, 120. The recording popover offers 15/30/60/120 as a **session override** that is not persisted | `macshot/UI/Windows/SettingsWindowController.swift:1729-1733`, `macshot/UI/Overlay/OverlayView+Popovers.swift:260-290` |
| When done | `recordingOnStop`: `editor` (default), `finder`, `clipboard`; the popover sets a session override | `macshot/AppDelegate.swift:2835-2845` |
| Editable pointer | `recordEditablePointer`, default true | `macshot/AppDelegate.swift:2796-2801` |
| Audio | `recordSystemAudio`, `recordMicAudio`, `selectedMicDeviceUID`; values read once at start | `macshot/AppDelegate.swift:2812-2814` |
| Live overlays | `recordMouseHighlight`, `recordKeystroke`, `keystrokeShowAll`, `recordWebcam`, camera device, webcam size, position and shape | settings catalogue; ledger |
| HUD | `hideRecordingHUD` | ledger |

The settings keys, types and effective defaults are ledger lines generated from MacShot source
([12](12-testing-strategy.md)); this table only names the ones that change recorder behaviour.

### 2.4 Normal mode and editable mode (CR-09)

The mode is decided once at start (`macshot/AppDelegate.swift:2948-2949`):

```text
editable = (effective "when done" == editor) AND recordEditablePointer
```

| Aspect | Normal mode | Editable mode |
| :--- | :--- | :--- |
| Cursor | baked into the video | hidden in the video; position and shape recorded as telemetry |
| Click ring, keystroke pill | visible live and baked into the pixels | recorded as telemetry, restyled in the studio |
| Webcam bubble | visible live and baked into the pixels | recorded to a separate camera track when its frames can be tapped; otherwise baked as in normal mode |
| Telemetry file | cursor position and shape are still recorded (the studio can zoom from them) | cursor, clicks and keys recorded |
| HUD, recording outline | never part of the recording | never part of the recording |

MacShot's source for the table: `macshot/AppDelegate.swift:2940-2975`, `macshot/Capture/RecordingEngine.swift:239-275`.
How Linux keeps own surfaces out of the pixels without an exclusion list is §9.4.

## 3. Sources, buffer paths and cadence

### 3.1 Stream request

`media::Recorder` asks the active `platform::CaptureBackend` for a stream
([02 §6](02-modules-and-interfaces.md)):

```cpp
struct StreamRequest {
    OutputId output;          // one output per recording
    QRect regionPx;           // even-sized device-pixel rectangle inside the output
    int maxFps;               // configured frame rate; frames closer than 1/maxFps are not delivered
    CursorMode cursor;        // Embedded (normal mode) | Hidden (editable mode) | Metadata (editable mode, portal)
    BufferPreference buffers; // DmaBuf (preferred) | Cpu
};
```

The result is a `StreamHandle`: either a `ScreenStream` that delivers `VideoFrame`s, or a `PipeWireNode` that `media`
consumes itself with `PipeWireVideoSource`. `media::VideoSource` hides the difference from the recorder.

A `VideoFrame` carries exactly one pixel payload — a DMA-BUF descriptor (file descriptors, strides, offsets, DRM
format and modifier), a CPU buffer, or a macOS `IOSurface` reference — plus the presentation timestamp on the
monotonic clock, the damage rectangles, the output transform, and optional cursor metadata (position, hotspot, shape
image). Payload buffers are owned by the source's pool; `media` releases each frame back to the pool as soon as the
GPU import or copy has completed and never retains a pool buffer, for the same reason MacShot copies pooled audio
(`macshot/Capture/RecordingSampleValidation.swift:56-87`).

### 3.2 Sources per platform

| Platform | Source | Notes |
| :--- | :--- | :--- |
| Hyprland | **[GATE G6]** candidate 1 or 2 below, whichever G6 selects | the other candidate stays as the fallback |
| Other wlroots | image-copy (candidate 1) where advertised, else ScreenCast | |
| KDE Plasma 6 | ScreenCast + PipeWire with a persistent restore token | image-copy only if the run-time probe shows KWin allows it (R9) |
| GNOME | ScreenCast + PipeWire; the picker appears each time unless a restore token restores the session | DEV-26 |
| X11 | XShm (or XComposite) grabs of the region at the configured frame rate; cursor through XFixes | |
| macOS | ScreenCaptureKit stream configured as MacShot configures it (below); VideoToolbox encode | |

**Candidate 1 — image-copy with DMA-BUF (the zero-copy target).** On AriadShot's own Wayland connection
([04 §3](04-capture-and-overlay.md)):

1. Create an `ext-image-copy-capture` session on the output's capture source, with `paint_cursors` set in normal mode
   and unset in editable mode.
2. From the session's DMA-BUF device and format/modifier announcements, allocate a small pool of DMA-BUF buffers with
   GBM on that device, choosing a format and modifier that VA-API can import. The **client** allocates; the protocol
   has no server-allocated DMA-BUF request.
3. For each `ready` frame, import the buffer into VA-API through DRM PRIME. VA-API video processing applies the output
   transform (a rotated output records upright), crops to `regionPx` and converts RGB → NV12 with BT.709 limited
   range. `h264_vaapi` encodes the NV12 surface.
4. Steady state: **zero CPU pixel copies per frame** [HYPOTHESIS, measured by G6].
5. Fallback when import fails: `wl_shm` buffers → libswscale (same BT.709 limited matrix) → upload. This path costs
   three CPU copies per frame and is measured separately.

**Candidate 2 — ScreenCast + PipeWire.** The portal backend creates the ScreenCast session (`persist_mode` 2, restore
token read from and rotated into the daemon's machine state after every start) and returns a `PipeWireNode` targeted
by its serial. `PipeWireVideoSource` negotiates DMA-BUF with modifiers and falls back to MemFd. Buffers flagged
`SPA_CHUNK_FLAG_CORRUPTED`, or without valid data or timestamp, are skipped and logged at debug level; they never
stop the stream. The same VA-API import and processing path follows. The cursor mode is embedded in normal mode and
metadata (`SPA_META_Cursor`) in editable mode.

**macOS — ScreenCaptureKit.** The stream configuration mirrors `macshot/Capture/RecordingEngine.swift:239-275`:
even pixel size of the region, `minimumFrameInterval` 1/fps, BGRA frames, scale to fit, the sRGB colour space set
explicitly (without it, Display P3 frames encode washed out), `showsCursor` false in editable mode, system audio with
the current process's own audio excluded, and a content filter that excludes AriadShot's HUD and recording outline —
plus, in editable mode, the click ring and keystroke pill panels, and the webcam panel when its frames are tapped.
Only frames whose status is *complete* and that carry an image buffer and a numeric timestamp are accepted
(`macshot/Capture/RecordingSampleValidation.swift:4-12`). `IOSurface` frames go to VideoToolbox through FFmpeg's
hardware frames.

**Encoder selection on Linux:** VA-API (`h264_vaapi`) when the render node supports import and encode; NVENC
(`h264_nvenc`) on NVIDIA; otherwise a software H.264 encoder — libx264 in distribution builds that link the system
FFmpeg, the runtime-downloaded OpenH264 binary in bundles ([13 §8](13-build-ci-release.md)).

### 3.3 Cadence and timestamps

- **Frame-rate cap.** A source delivers at most one frame per 1/fps: SCK through `minimumFrameInterval`, the other
  sources by not submitting a frame earlier than one frame duration after the previous accepted frame.
- **Variable frame rate.** Image-copy and ScreenCast deliver frames only when the screen changes. Each frame is
  stamped from its `presentation_time` (image-copy) or the buffer PTS on `CLOCK_MONOTONIC` (PipeWire), mapped to
  session time (§2.1); the encoder receives variable-frame-rate timestamps. The writer's video time base is 1/60000,
  MacShot's capture timescale (`macshot/Capture/VideoFrameCadence.swift:107`).
- **Repeated captures.** An image-copy session's next frame waits for damage; the recorder therefore keeps one frame
  request outstanding at all times and never polls.
- **Heartbeat.** A 50 ms maintenance timer checks the time since the last video frame; after ≥ 1 s without one, the
  last frame is re-submitted at the current time (`macshot/Capture/MP4WriterSession.swift:165-186,334-343`). This
  keeps audio interleaving and fragment boundaries advancing on a static screen, and retaining one frame does not grow
  memory with idle time.
- **Rational cadence.** The movie records the configured frame duration as a rational value in its metadata, as
  MacShot's software tag `macshot; frame-duration=<num>/<den>` does (`macshot/Capture/VideoFrameCadence.swift:5-14`).
  AriadShot writes `ariadshot; frame-duration=<num>/<den>`; readers accept both prefixes, so the studio opens MacShot
  takes with their exact cadence.

### 3.4 Budgets

| Budget | Target | Workload |
| :--- | :--- | :--- |
| 1080p60 recording, per candidate | ≤ 15 % of one core, zero dropped frames, ≤ 1 CPU pixel copy per frame | W4: 10 minutes of a scrolling browser page, VA-API |
| Studio preview | 60 fps p95 | W5: a 1080p take with every look enabled, QRhi Vulkan on the iGPU |

Both are [HYPOTHESIS] until G6 and G8 record baselines; a missed budget is revised with numbers, never silently
([12](12-testing-strategy.md), [14 §2](14-gates-and-milestones.md)).

## 4. Encoder and bitrate policy (CR-10)

Live recording always encodes **H.264, quality High** into MP4 (`macshot/Capture/MP4WriterSession.swift:100-101`).
The encoder settings are shared with offline export (`macshot/Capture/VideoEncodingSettings.swift:131-156`):

- no B-frames; CABAC entropy coding;
- one keyframe per second (GOP = fps frames, and at most 1.0 s apart also for variable-rate input);
- BT.709 primaries, transfer and matrix, tagged in the stream and the container; limited range;
- profile High for quality Medium and High, Main for Low (`macshot/Capture/VideoEncodingSettings.swift:35-41`).

Target bitrate (`media::BitratePlan`, a port of `macshot/Capture/VideoEncodingSettings.swift:106-124`):

```text
pixels = width × height
base   = pixels × fps × bpp(quality)                bpp: Low 0.12, Medium 0.22, High 0.40
taper  = 0.80 if pixels > 3840×2160; 0.92 if pixels > 1920×1080; else 1.0
raw    = base × codecFactor × taper                 codecFactor: H.264 1.00, HEVC 0.88
bitrate = clamp(raw, min(quality), max(quality))    Low 1–12 Mbps, Medium 4–30 Mbps, High 10–80 Mbps
```

Rounded to the nearest integer; a non-positive dimension or frame rate returns the quality's minimum. HEVC appears
only for completeness of the port (MacShot uses it for MOV containers); AriadShot writes H.264 in MP4 on every
platform. Hardware encoders receive the target as their average bitrate in variable-bitrate mode.

The camera track (editable mode) is encoded separately: H.264, width at most 1280 (720p class) with even
dimensions, 4 Mbps average, no B-frames, media time zero aligned to the screen's first frame
(`macshot/Capture/VideoCameraRecorder.swift:19-20,100-104`). On Linux and macOS the camera is read through Qt
Multimedia's camera API (`media::CameraSource`) [HYPOTHESIS: its frame timestamps are usable for alignment; verified
in M3].

## 5. Audio tracks (CR-11)

| Track | Order | Format | Source (Linux / macOS) |
| :--- | :--- | :--- | :--- |
| Video | added first | §4 | §3 |
| Microphone | **first audio track** when enabled | AAC-LC, 48 kHz, mono (downmixed), 128 kbit/s | PipeWire capture stream on the selected device, default device when unset / AVCapture through `platform::MicrophoneSource` |
| System audio | next audio track when enabled | AAC-LC, 48 kHz, stereo, 256 kbit/s | PipeWire capture of the default sink's monitor / the ScreenCaptureKit stream's audio |

Source: `macshot/Capture/MP4WriterSession.swift:112-153`. The microphone is the first audio track so players that
decode only one audio track play the narration. No absolute container track index is promised beyond this order.

Rules (`media::AudioInput`, `media::Mp4Writer`):

- **Owned copies.** Audio from pooled buffers is copied into owned memory before it is queued
  (`macshot/Capture/RecordingSampleValidation.swift:56-87`). Buffers that are incomplete or not PCM are rejected
  (`:14-28`).
- **Clock alignment.** Every audio buffer is stamped on the session clock. On macOS the microphone's capture-session
  clock is converted to the host clock as MacShot does (`macshot/Capture/MicrophoneCapture.swift:72-76`); PipeWire
  buffers carry monotonic timestamps directly.
- **Overlap trimming.** When a buffer starts before the end of the previous one, the overlapping leading samples are
  trimmed. Trimming works per channel on planar PCM, advancing each channel by `skipped × bytesPerFrame`
  (`macshot/Capture/RecordingSampleValidation.swift:29-49,117-152`).
- **Bounded queues.** Each pending audio queue holds at most 10 s and 4096 buffers
  (`macshot/Capture/RecordingSampleValidation.swift:166-183`). Before the first video frame, at most 5 s of pre-roll is
  kept and older buffers are dropped (`macshot/Capture/MP4WriterSession.swift:436-447`).
- **Fixed format.** Each PipeWire stream requests 48 kHz float with a fixed channel count and lets PipeWire convert; a
  change of the negotiated format during the take is the `audioFormatChanged` failure, as in MacShot (§7).

## 6. Writer states and crash recovery

MacShot writes 10 s movie fragments (`macshot/Capture/MP4WriterSession.swift:96-99`). FFmpeg's `frag_keyframe`
starts a fragment at every keyframe, so with one keyframe per second AriadShot's fragments are about 1 s long — a
finer recovery granularity, recorded as **DEV-29**. `media::Mp4Writer` moves through explicit states:

| State | What happens | Leaves when |
| :--- | :--- | :--- |
| `writing` | fragmented MP4 with `empty_moov+default_base_moof+frag_keyframe`; `avio_flush` after every fragment | stop, a failure (§7), or a crash |
| `syncing` (timer, overlaps `writing`) | `fdatasync` of the movie file at least once every 2 s; the take status file records the last synced fragment | every tick |
| `finalizing` | write the trailer; 15 s watchdog | trailer written → `publishing`; watchdog fires → take marked `interrupted` |
| `publishing` | stream-copy remux into a **non-fragmented** MP4 with `+faststart` in a temporary file in the take folder, `fsync`, then rename without replacing (the atomic publish of [07](07-storage-history-settings.md)); the fragmented file is removed only after the published file and the status file are durable | published → take `complete` |
| `recovering` (next launch) | the status file still says `recording`: open the fragmented file with libavformat, keep complete fragments, remux them through `publishing`, mark the take `interrupted`, and show it in Recent Captures and the studio with a notice | done |

- The telemetry file is flushed to the kernel every 0.5 s and synced on the writer's `syncing` tick, so its durable
  prefix matches the movie's (MacShot pairs telemetry syncs with its fragment interval,
  `macshot/Capture/CursorTelemetryRecorder.swift:36-56,220-223`). A telemetry write failure closes the telemetry file
  and never stops the take (`:44-47`).
- The camera track is written the same way (fragmented, flushed, synced on the same tick) and published with the take.
- **Durability claim.** Recovery is tested, not assumed: at least 200 `kill -9` trials at random points of 10-minute
  takes; in at least 95 % of trials the published file is playable up to the last synced fragment according to two
  independent demuxers, FFmpeg's libavformat and GPAC `MP4Box`, used as test tools only
  ([12](12-testing-strategy.md)). Power loss is outside the claim.

## 7. Failure states and watchdogs (CR-12)

Each failure stops the take through `stopping`, keeps whatever was written, and reports the error with MacShot's
message through the failure toast "Recording failed: %@" (`macshot/AppDelegate.swift:2826-2829`). A take with a
playable partial file is still delivered through the when-done route (§10).

| Failure | Trigger | Source |
| :--- | :--- | :--- |
| `noFrames` | no complete video frame within 10 s of start | `macshot/Capture/MP4WriterSession.swift:165-179` |
| `audioOverload` | a pending audio queue would exceed 10 s or 4096 buffers | `macshot/Capture/MP4WriterSession.swift:425-433` |
| `audioFormatChanged` | an audio buffer's format differs from the configured one | `macshot/Capture/MP4WriterSession.swift:418-422` |
| `appendFailed` | the encoder or muxer rejects a sample (for example, disk full) | `macshot/Capture/MP4WriterSession.swift:56-63` |
| `finalizationTimedOut` | the trailer is not written within 15 s | `macshot/Capture/MP4WriterSession.swift:297-302` |
| Out of space | free space on the take's file system < 256 MiB, checked every 2 s | `macshot/Capture/RecordingEngine.swift:447-460` |
| System sleep | the machine is about to sleep: stop gracefully before it does | `macshot/Capture/RecordingEngine.swift:291-296` |

The error strings are MacShot's catalogue strings (`macshot/Capture/MP4WriterSession.swift:59-63`,
`macshot/Capture/RecordingEngine.swift:504-511`), rewritten only where they name macOS ("the Mac") — those Linux
variants are catalogue additions, not deviations in behaviour.

Sleep signal per platform: `NSWorkspace.willSleepNotification` on macOS; on Linux the logind `PrepareForSleep(true)`
signal, with a delay inhibitor lock held for the duration of a take so the stop can complete before suspend.

## 8. Telemetry capture (CR-15)

The telemetry file (format in [06](06-annotation-model-and-formats.md)) records, on the session clock: cursor moves
(normalised to the recorded region), button presses, cursor shape changes with deduplicated shape images, key events,
and start, pause and resume markers. It replaces MacShot's `cursor.mstl` with an AriadShot format carrying the same
records and semantics (DEV-28). A MacShot file importer is outside the required scope ([06 §1](06-annotation-model-and-formats.md)).

MacShot polls the pointer at 120 Hz, suppresses records while the pointer is stationary and inserts a hold sample when
movement resumes after more than 20 ms, samples the cursor shape at 15 Hz, and stores each distinct shape once
(`macshot/Capture/CursorTelemetryRecorder.swift:147-160`, `macshot/Capture/CursorMotion.swift:71-73`). AriadShot
keeps the same record semantics; its sources are event-driven where the platform offers events:

| Data | Hyprland / wlroots | KDE, GNOME | X11 | macOS |
| :--- | :--- | :--- | :--- | :--- |
| Cursor position and shape | image-copy cursor session on the recorded output | ScreenCast cursor metadata (`SPA_META_Cursor`) | XFixes cursor image + pointer query at 120 Hz | CGEvent polling at 120 Hz, `NSCursor` shape at 15 Hz |
| Buttons | opt-in input helper (DEV-24) | opt-in input helper | XInput2 raw events | CGEvent button state |
| Keys | opt-in input helper | opt-in input helper | XInput2 raw events, translated with xkbcommon | listen-only event tap (Input Monitoring) |

- Cursor-session and metadata positions arrive at the compositor's rate, not at a fixed 120 Hz; the stationary
  compression and hold-sample rules apply unchanged [HYPOTHESIS: the delivered rate is sufficient for the spring model
  of §11; measured in M3].
- Without clicks and keys (Wayland without the helper), the recorder still writes cursor records. The studio then
  disables the looks that need clicks or keys — click effects, keystrokes, Auto Zoom — and shows MacShot's
  explanatory notes with a "How to enable" link to the Desktop Integration page (DEV-24,
  [08 §1](08-platform-integration.md)).
- The input helper's privacy contract (what it may read, when, and where it may send it) is fixed in
  [11 §6](11-security-privacy.md).

## 9. Live overlays, HUD and self-exclusion

All live overlays are overlay-class surfaces drawn by `ui/` view objects through the active surface host
([04 §5](04-capture-and-overlay.md)).

### 9.1 Click ring (CR-17) and keystroke pill (CR-18)

- **Click ring.** On left or right button press, a `systemYellow` ring at the press point: radius 18 + 60·age pt, alpha
  1 − age/0.3, fill at 0.35·alpha, 2 pt stroke at 0.6·alpha, removed after 0.3 s
  (`macshot/UI/Overlay/MouseHighlightOverlay.swift:74-105`).
- **Keystroke pill.** Bottom centre of the recording area, 40 pt above its bottom edge; black at 0.65, radius 14,
  1 pt white border at 0.15, padding 24 × 14, system font 28 pt medium; held 1.5 s, then faded by 0.05 per tick at
  30 fps (`macshot/UI/Overlay/KeystrokeOverlay.swift:250-331`). With `keystrokeShowAll` off, only combinations that
  include a command-class modifier are shown (`:102,127`). On Linux the modifiers ⌘, ⌥ and ⌃ map to Super, Alt and
  Ctrl for this filter, and labels use the Linux key names (DEV-43).
- Both need global pointer and key events: available on macOS (Input Monitoring for keys), X11, and Wayland only with
  the opt-in helper (DEV-24). Without it the toolbar buttons are shown disabled with the reason
  ([08 §1](08-platform-integration.md)).

### 9.2 Webcam bubble (CR-19)

A draggable surface above the capture overlay (MacShot level 258, one above the overlay's 257,
`macshot/UI/Overlay/WebcamOverlay.swift:81`), circle or rounded rectangle with radius size/5, 2 pt white border at
0.5, size 80–480 pt (default 120, presets 80/120/160/220), clamped to the selection minus 24 pt, placed in one of four
corners 12 pt inside the selection (`:14-21,97,271`). Position, size and shape persist. On GNOME the bubble cannot be
placed or kept on top (DEV-26).

### 9.3 Recording HUD (CR-20) and status-item stop (CR-21)

- **HUD.** 164 × 32 pt, radius 10, background `#1F1F1F` at 0.94 with a 0.5 pt icon-colour border at 0.10; stop
  button, pause/resume button, record dot (red, orange while paused), `00:00` monospaced timer, divider and drag
  handle; placed 8 pt above the region and flipped 8 pt below when there is no room; once dragged it is never
  repositioned automatically; hidden with `hideRecordingHUD` (`macshot/UI/Overlay/RecordingHUDPanel.swift:20-131`).
  It is a non-activating surface (role `RecordingHud`, no keyboard).
- **Status item during recording.** The item is forced visible even if the user hid it, shows a stop icon, loses its
  menu, and stops the recording on click; the icon and menu are restored afterwards
  (`macshot/AppDelegate.swift:3000-3020`). On GNOME without the AppIndicator extension there is no tray; the stop
  controls are then the HUD, the Record hotkey and the notification action (CR-21, DEV-26).

### 9.4 Keeping AriadShot's own surfaces out of recordings (CR-23)

macOS uses ScreenCaptureKit's exclusion list exactly as MacShot does (§3.2), including MacShot's camera fallback: if
the camera recorder fails mid-take, the webcam panel is removed from the exclusion list so the bubble is baked into
the screen video instead (`macshot/Capture/RecordingEngine.swift:307-319`).

No Wayland capture protocol offers per-surface exclusion, so on Linux (all desktops) the recorder follows this
strategy:

| Surface | Region recording | Full-screen recording |
| :--- | :--- | :--- |
| Recording outline (OV-30) | drawn outward, outside the region, so it is never inside the recorded pixels | not shown |
| HUD | placed outside the region (8 pt above, else below, else on another output); if no position outside the region exists on any output, hidden | moved to another output if one exists; otherwise hidden, with the status item and the Record hotkey as stop controls and a one-time notice (DEV-22) |
| Webcam bubble, editable mode | shown only where it lies outside the recorded region; otherwise hidden while the camera track still records (DEV-23) | hidden while the camera track records (DEV-23) |
| Click ring and keystroke pill, editable mode | recorded as telemetry only; not drawn over the recorded region (DEV-23) | same |
| Normal mode overlays | visible and baked into the pixels, as in MacShot | same |

Hyprland's `no_screen_share` window rule is tested in M3 for its effect on layer surfaces and image-copy capture; the
strategy above does not depend on it, and it is adopted only if the test shows it excludes surfaces reliably.

## 10. Finishing a take

### 10.1 Audio merge (CR-13)

When both microphone and system audio were recorded (≥ 2 audio tracks), the audio-merge dialog opens before the
when-done route (`macshot/AppDelegate.swift:2847-2858`):

- 380 × 160 pt floating panel "Audio Tracks": header "Adjust volume for each audio track:", sliders "Microphone:" and
  "System audio:" (0–1, default 1, continuous), buttons "Keep Separate" (Esc) and "Merge Audio" (Return)
  (`macshot/UI/Windows/AudioMergeController.swift:28-90`).
- Merge (`media::AudioTrackMixer`): the video stream is copied without re-encoding; the audio tracks are decoded,
  mixed with gains `volumeᵢ / max(1, Σ volumes)` so the sum never clips, and re-encoded as one stereo AAC track at
  48 kHz, 256 kbit/s into a new file in the take folder (`macshot/Capture/AudioTrackMixer.swift:59-99`). The merged
  file becomes the take's movie; the original stays in the take folder.

### 10.2 When-done routes (CR-24)

| Route | Behaviour | Source |
| :--- | :--- | :--- |
| `editor` (default) | open the take in the studio | `macshot/AppDelegate.swift:2836-2844` |
| `finder` | publish a user-visible copy — into the recording save folder, else the screenshot save folder, else through a save dialog — with " (N)" collision suffixes, then reveal it in the file manager (SH-34; `FileManager1.ShowItems`, else open the folder through OpenURI). Cancelling the dialog leaves the take in the library | `macshot/AppDelegate.swift:3026-3070` |
| `clipboard` | put the movie on the clipboard as a file reference; a GIF of at most 32 MB is also put as raw GIF data; plays the copy sound | `macshot/AppDelegate.swift:3109-3131` |

The take itself always stays in the recording library. On Wayland the clipboard route runs after the overlay has gone,
so it needs the background clipboard (data-control, [08](08-platform-integration.md)); on GNOME, where no
background clipboard exists, the route falls back to `finder` with a one-line notice (DEV-26).

## 11. Cursor motion model (CR-16)

`core::CursorSpring` ports `macshot/Capture/CursorMotion.swift` bit for bit in double precision; the studio uses it to
restyle the pointer from telemetry.

- **Spring.** Critically damped, ω = 34 − 27·smoothing (`:80`), evaluated at 120 Hz with four sub-steps per output
  frame, exact step per `dt` (`:138-146`):
  `offset = x − target; decay = e^(−ω·dt); temp = (v + ω·offset)·dt; x' = target + (offset + temp)·decay;
  v' = (v − ω·temp)·decay`.
- **Sway.** `tilt = −tanh(v·0.9) · 0.24 · amount`, with the velocity taken over 0.04 s on the smoothed path
  (`:186-199`).
- **Press dip and release.** Press 0.07 s from 1.0 to 0.82, held while the button is down, release 0.18 s with
  `scale = 0.82 + 0.18·(1 − (1 − r)³) + 0.06·sin(r·π)` (`:220-234`).
- **Fades.** Idle fade-out 0.35 s and fade-in 0.12 s after the configured idle delay (`:51`); typing fade 0.15 s
  (`:171`); movement bursts use ε 0.0008 and a 0.25 s gap (`:71-73`).
- **Pointer motion blur.** Shutter = (1/40 s) × motion blur; applied when the pointer moved more than 4·max(1, scale/2)
  px within the shutter, with min(12, max(3, distance/6)) samples (`macshot/Capture/VideoSceneRenderer.swift:138,355-368`).

## 12. Studio editor: document, window and interaction

### 12.1 Document (VE-01, VE-13)

- The source video is read-only. All edits live in the project document (`core::VideoProject`; format in
  [06](06-annotation-model-and-formats.md)): trim, crop (normalised, minimum 0.05), look, zooms, censors, texts, cuts,
  speeds, freezes, captions.
- Location: beside the take for takes AriadShot owns; for any other video, in an external project folder keyed by the
  first 12 bytes of the SHA-256 of the standardised path, written as 24 hex characters
  (`macshot/UI/Editor/Video/VideoEditorDocument.swift:144-151`). Paths are in [07](07-storage-history-settings.md).
- **Undo.** Up to 200 full encoded snapshots; redo is cleared by a new edit; continuous gestures (slider drags,
  timeline and stage handles) are grouped into one step between `beginGesture` and `endGesture`
  (`macshot/UI/Editor/Video/VideoEditorDocument.swift:185-203`).
- **Autosave.** Debounced 1.2 s after the last edit (`:312-316`); synchronous save when the window closes and when the
  application quits (`:138-141`, `macshot/UI/Editor/VideoEditorWindowController.swift:272`).

### 12.2 Window (VE-09)

`windows::StudioWindow` ports `macshot/UI/Editor/VideoEditorWindowController.swift` and `macshot/UI/Editor/Video/*`:

- dark appearance always; minimum 980 × 640 pt; initial size
  `min(0.9·W, max(1100, 0.84·W)) × min(0.92·H, max(720, 0.86·H))` of the screen's visible area (`:87-98`);
- top bar 52 pt, stage toolbar 44 pt, transport 50 pt, stage inset 28 pt;
- inspector: rail 56 pt + panel 304 pt with six sections — Background, Pointer, Zoom, Keystrokes, Camera, Captions
  (`macshot/UI/Editor/Video/VideoInspectorView.swift:7-31,46-47`);
- timeline 230 pt (minimum 180): leading inset 14, trailing 24, ruler 26, clip lane 58 (thumbnails and waveform),
  lanes 30, lane gap 6 (`macshot/UI/Editor/Video/VideoTimelineView.swift:36-43`);
- colours are the palette of `macshot/UI/Editor/Video/VideoEditorStyle.swift:6-26` (window, panel, raised panel,
  stage, controls, text tiers, and one colour per timeline item kind); the palette is a ledger table, not restated here.

### 12.3 Keys (VE-10)

The key matrix is `macshot/UI/Editor/VideoEditorWindowController.swift:560-609`, generated into the ledger. Rules
that the matrix alone does not show:

- while inline text is being edited, every studio shortcut is ignored (`:564`);
- undo and redo use the customisable editor chords (SH-12); every ⌘ chord is Ctrl on Linux;
- Esc cancels a running export first, else leaves crop mode, else clears the selection (`:575-579`);
- timeline zoom keys (`=`/`+`, `−`) take no modifier.

### 12.4 Timeline (VE-11)

`windows::StudioTimeline` ports `macshot/UI/Editor/Video/VideoTimelineView.swift`:

- click or drag on the ruler or an empty lane scrubs; a hover line follows the pointer;
- trim handles respond within 8 pt and keep `trimEnd − trimStart ≥ 0.1 s`;
- dragging an item's body moves it; dragging within 6 pt of an edge resizes it with a 0.2 s minimum (`:728-730`);
- snapping within 7 pt on screen to the playhead, trim start and end, 0, the source duration, and the edges of every
  other item (`:745-758`);
- double-click on an empty lane (not the clip or captions lane) adds a segment of that lane at that time;
  double-click on a text item edits it inline;
- text and censor items share the overlays lane and are packed into rows greedily in start order (`:218-228`).

### 12.5 Stage (VE-12)

`windows::StudioStage` is a `QRhiWidget` (Vulkan requested explicitly on Linux, OpenGL fallback, Metal on macOS)
showing `FrameScene` renders ([03 §3](03-rendering-contracts.md)), with an editing layer on top:

- crop, censor and text items have eight handles; zoom rectangles have four corner handles and resize uniformly about
  the opposite corner, keeping the canvas aspect; minimum normalised size 0.03
  (`macshot/UI/Editor/Video/VideoStageView.swift:336-349`);
- moving a following or automatic zoom pins it (`followsCursor` and `isAutomatic` become false, `:377`);
- inline text editing uses the segment's font scaled by displayed height / 1080; Return commits, Esc cancels
  (`:394-450`);
- editing aids show only while paused; clicking the stage pauses playback; while a spatial item is selected or crop is
  active, the camera is suspended so content stays under the handles (`:95-119`);
- the preview renders at the canvas's on-screen pixel size, with the scale rounded up to a multiple of 0.05
  (`:86-91`).

## 13. Studio looks, segments and auto-zoom

Fields, ranges and defaults are part of the project format ([06](06-annotation-model-and-formats.md)); this section
states what each one does. Source: `macshot/Model/VideoProject.swift:25-292` and the segment models in
`macshot/Model/Video*Segment.swift`.

### 13.1 Looks (VE-02 to VE-05)

- **Frame.** Aspect `auto` or one of eight fixed ratios; padding as a fraction of the recording's shorter side;
  corner radius in points at a 1080-pixel reference height; two-pass shadow; optional hairline border (white at 0.22,
  width max(1, short/1080 × 1.5)). A background is drawn when framing is enabled or the aspect is not `auto`.
- **Background.** Gradient (from the beautify catalogue, default `linear-0`), colour, image (copied into the project),
  or wallpaper; blur with σ = blur × short × 0.045. The wallpaper kind reads the desktop's current wallpaper where the
  desktop exposes it through `platform::WallpaperSource`, and the option is hidden where it does not (DEV-19).
- **Pointer.** Appearance system, dot or ring; size; smoothing (§11); hide when idle with a delay; hide while typing;
  click effect none, ripple, spotlight or ring, with a colour; press bounce; motion blur; loop to start; sway.
- **Zoom.** Default level; transition gentle (1.2 s), smooth (0.85 s) or snappy (0.5 s) with smootherstep easing;
  connect zooms whose gap is ≤ 1.2 s by panning directly; camera motion blur; follow dead zone. The camera follows with
  a critically damped spring of ω 4.2 and pans between targets with ω 5.5
  (`macshot/Capture/VideoSceneGeometry.swift:194-197`). Camera motion blur uses a (1/40 s) × amount shutter, is
  skipped when the canvas corners move ≤ 1.5 pt, and takes min(10, max(3, displacement/3)) samples
  (`macshot/Capture/VideoSceneRenderer.swift:242-275`).
- **Keystrokes, camera, captions.** Nine anchor positions; sizes; keystrokes shortcuts-only and light appearance;
  camera shape, mirror, shrink on zoom and shadow; captions font size at a 1080 reference, background and maximum
  words per caption.

### 13.2 Segments (VE-06)

| Segment | Behaviour |
| :--- | :--- |
| Zoom | level 1.2–5, fades 0.35 s, minimum 0.3 s; follows the cursor or holds a centre; automatic zooms are marked |
| Censor | solid, pixelate (20 pt blocks) or blur (30 pt radius); applied to source pixels before crop and camera; minimum 0.3 s; rectangle minimum 0.02 |
| Text | 6–400 pt (default 48), styled card, fades 0.25 s, minimum 0.3 s; follows content through the camera |
| Cut | removes a source range, minimum 0.1 s; overlapping cuts merge |
| Speed | factor 0.25–10; audio is resampled with the speed, so pitch shifts as in MacShot's time scaling |
| Freeze | holds one source frame for 0.1–30 s (default 1 s) and inserts silence |

`core::TimeMap` turns trims, cuts, speeds and freezes into ordered pieces (normal, speed, freeze) with source and
composition durations, as `VideoRenderPlanner.pieces` does; the preview clock, export, SRT timing and captions all use
it. The composition layer order is MacShot's six stages — censors, framed recording with background, camera
transform with motion blur, output-space text, pointer and click effects, then the screen-fixed webcam, keystrokes
and captions drawn in that order (`macshot/Capture/VideoSceneRenderer.swift:153-231`); the pass list that implements
it is in [03 §3](03-rendering-contracts.md).

### 13.3 Auto-zoom (VE-07)

`core::AutoZoomPlanner` ports `macshot/Capture/VideoSceneGeometry.swift:343-395`:

1. Events: every click in range; a key counts only when it comes more than 0.8 s after the previous key.
2. Clusters: events sorted by time; consecutive events ≤ 2.4 s apart share a cluster.
3. Timing: start = first event − 0.7 s; end = last event + 1.5 s; extended to at least 2.0 s; clamped to the trim;
   dropped if shorter than 0.6 s after clamping; overlapping suggestions merge.
4. Focus: the first event's point. Level: `min(level, max(1.25, 1 / (max(Δx, Δy) + 0.18)))`.
5. Suggestions that overlap existing zooms are skipped.

The inspector's Auto Zoom, Remove Automatic Zooms and Apply Default Zoom to All actions operate on these segments
(`macshot/UI/Editor/Video/VideoInspectorView.swift:490-620`). Auto Zoom needs click or key telemetry; without it the
button is disabled with MacShot's note (`:497`).

## 14. Decode, seek and playback

- `media::Decoder` decodes with FFmpeg using VA-API (Linux) or VideoToolbox (macOS) hardware contexts, falling back
  to software, and uploads frames into a GPU texture LRU of 24–36 frames. Sequential playback and scrubbing are served
  from the cache; a non-sequential seek goes to the preceding keyframe and decodes forward to the target.
- The camera track is decoded the same way and composited as the webcam layer.
- Audio plays through Qt Multimedia's audio sink. Speed segments play with varispeed resampling (pitch follows
  speed); freezes play silence; mute follows the project's `muted` flag.
- Preview renders `FrameScene`s at the playback clock; export renders them at the exact rational cadence
  ([03 §3](03-rendering-contracts.md)).

## 15. Export (VE-14, ED-12)

Export settings persist as in MacShot (`macshot/UI/Editor/Video/VideoEditorExporter.swift:5-34`): scale in (0, 1]
(anything else becomes 1), quality Low/Medium/High (default High), format MP4 or GIF, GIF frame rate 5–30 (0 or
missing means 15).

**Render only when needed** (`:54-59`). Rendering is required when the format is GIF, the scale is below 0.999, the
quality is not High, the project has edits, the cursor or keystrokes are editable telemetry, or a camera track exists.
Otherwise the source file is copied byte for byte through the atomic publish (`macshot/UI/Editor/VideoEditorWindowController+Export.swift:93-110`).

**Rendered MP4.** `media::Exporter` builds `FrameScene`s at the output cadence on its own offscreen `QRhi`, converts to
NV12 in one pass, encodes H.264 with the §4 settings, and muxes the mapped audio (AAC, 48 kHz, 128 kbit/s per track,
`macshot/Capture/VideoTranscoder.swift:4,95-96`). The video bitrate follows MacShot's export plan
(`macshot/Capture/VideoExportEncodingPlan.swift`):

```text
(w, h) = even(canvas × scale);  target = w × h × fps × bpp(quality)
floor, ratio = Low 64 kbit/s, 0.5 | Medium 128 kbit/s, 0.8 | High 256 kbit/s, 1.0
if the source is H.264 with a near-uniform cadence (nominalFps × minFrameDuration in 0.9–1.1):
    target = min(target, sourceBitrate × ratio × areaRatio^0.75 × clamp(sourceDuration/outputDuration, 1, 10))
bitrate = min(max(quality), max(floor, target))
```

MacShot routes High through Apple's highest-quality export preset, whose bitrate Apple chooses; AriadShot uses the same
plan with quality High on every platform, because the export encoder is FFmpeg everywhere (ADR 0001; DEV-42).

**GIF (CR-14).** `media::GifWriter` streams GIF89a without a global palette: a looping NETSCAPE2.0 extension; per
frame, a local palette from libimagequant; unchanged frames extend the previous frame's delay instead of being
written; changed frames are cropped to the changed rectangle and written with disposal method 1; delays use the GIF
100 Hz clock and are split above 65 535 ticks (`macshot/Capture/GIFEncoder.swift`). Memory is bounded to the current
and previous frames. In the pinned source the GIF writer is used by studio export, fed at the chosen GIF cadence
(`macshot/Capture/GIFExporter.swift:54`).

**SRT.** Captions are written as SubRip on the edited clock: freezes add their duration after their source time,
fully passed pieces add their composition duration, and time inside a piece is divided by its speed factor
(`macshot/UI/Editor/Video/VideoEditorExporter.swift:159-172`).

**Progress and cancel.** A modeless 420 × 130 pt progress window shows the job
(`macshot/UI/Windows/MediaExportProgressController.swift:29`); Esc in the studio or the window's cancel button
cancels; publication is atomic and a cancelled export leaves no partial file. Copy (⌘C) and upload of a video use the
same export job with a temporary destination.

## 16. Captions (CR-22, VE-15)

- Generate, regenerate, export (.srt) and remove captions from the inspector. The note "Captions are transcribed on
  this Mac. Nothing is uploaded." is kept, with "this computer" on Linux
  (`macshot/UI/Editor/Video/VideoInspectorView.swift:599-605`).
- Engine: `ml::CaptionTranscriber` — Speech with on-device recognition required on macOS
  (`macshot/Capture/VideoCaptionTranscriber.swift:30,84`); whisper.cpp with an on-demand model on Linux, behind the
  model consent flow ([09](09-ml-services.md)). Transcription never uses the network.
- Audio is transcribed in 50 s chunks (`macshot/Capture/VideoCaptionTranscriber.swift:24-56`); words become caption
  segments of at most `maxWords` words, split at gaps over 0.9 s, each at least 0.4 s long
  (`macshot/Capture/KeystrokeTimeline.swift:109-120`). A take without audio shows "This recording has no audio to
  transcribe." (`macshot/UI/Editor/Video/VideoInspectorView.swift:599`).

## 17. Opening videos that are not takes (SH-30)

Open Video…, the `open?file=` action for `mp4`, `mov` and `m4v`, file-manager "Open With" and drops route videos to
the studio (images, including GIF, go to the image editor). A video without telemetry opens with the pointer restyle,
zoom-from-clicks and keystroke looks disabled, each with MacShot's reason — for example "This video has no recorded
pointer data. …" (`macshot/UI/Editor/Video/VideoInspectorView.swift:438-440,497,543-544,574`). Its project is stored
in the external project folder (§12.1). A take whose video already contains the pointer (normal mode) cannot restyle
it, but zooms can still follow it (`:438`).

## 18. Verification

Subsystem tests follow [12](12-testing-strategy.md); the ones specific to this file:

- defaults and constants (bitrate tables, clamps, audio formats, watchdog times, HUD geometry) asserted against ledger
  values;
- lifecycle, clock and time-map unit tests ported from MacShot's own tests (`macshotTests/RecordingLifecycleTests.swift`,
  `RecordingClockTests.swift`, `VideoTimelineMappingTests.swift`, `VideoSpeedAndFreezeTests.swift`);
- writer state-machine tests with injected failures for every row of §7;
- telemetry codec tests with truncated trailing records and AriadShot-format fixtures;
- the studio contract tests of G5 ([03 §3](03-rendering-contracts.md));
- G6 measurements and the 200-trial kill test of §6 on the reference host;
- frame-timed checks of the click ring, keystroke pill fade and HUD placement ([12](12-testing-strategy.md)).
