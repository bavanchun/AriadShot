<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 02. Modules and Interfaces

Part of the [AriadShot technical specification](README.md). This file defines every CMake module: what it owns, its
public interface, and which modules and libraries it may depend on. Each module's allowed list is the `ALLOWED`
argument of its `ariadshot_add_module(…)` call in `src/<module>/CMakeLists.txt` (the daemon executable's is its
`ariadshot_architecture_register(…)` call in `src/app/CMakeLists.txt`). `cmake/AriadShotArchitecture.cmake` checks
those lists and the portable modules' reachability at configure time, and `scripts/check-architecture.sh` checks
include directives; when they and this file disagree, fix one of them in the same pull request.

## 1. Module graph

Each module is one CMake target `ariadshot_<module>` with the alias `AriadShot::<module>`, declared with
`ariadshot_add_module(NAME … KIND STATIC|INTERFACE ALLOWED …)`. A module without code is an INTERFACE target that
declares its place in the graph and nothing else: no sources and no placeholder code. It becomes STATIC in the pull
request that adds its first real class. Module `CMakeLists.txt` files and every INTERFACE → STATIC switch are shared
files owned by the manager role (see `docs/dev/agent-workflow.md`); they land before parallel work starts in a module.

| Module | Directory | May link directly | Include rule | Owns |
| :--- | :--- | :--- | :--- | :--- |
| `core` | `src/core/` | `Qt6::Core` | QtCore and `core/` only; no QtGui, QtWidgets, QtQuick, `qpa/`, `private/` or platform headers | models, value types, non-pixel algorithms, stores, formats |
| `render` | `src/render/` | `core`, `Qt6::Gui`; vendored pocketfft | adds QtGui and `render/`; no QtWidgets, QtQuick or platform headers | canonical images, renderers, text engine, effects, encoders, the audited pixel module |
| `ui` | `src/ui/` | `core`, `render`, `platform`, `Qt6::Gui` | adds `ui/` and `platform/` headers; **no QtWidgets** | host-agnostic view objects, canvases, chrome, tool handlers, canvas text control |
| `platform` | `src/platform/` | `core`, `Qt6::Gui` | adds `platform/` | interfaces only, plus the capability vocabulary |
| `hosts` | `src/hosts/` | `ui`, `platform`, `Qt6::Widgets`; per G1 outcome `Qt6::Quick`, `LayerShellQt`, `wayland-client`, Cocoa (ObjC++), xcb | adds `hosts/` | surface hosts |
| `windows` | `src/windows/` | `ui`, `render`, `core`, `Qt6::Widgets`; from M4 `media` and `Qt6::GuiPrivate` (studio stage) | adds `windows/` | Qt Widgets windows and dialogs, the studio frame |
| `backends/wayland` | `src/backends/wayland/` | `platform`, `wayland-client`, `Qt6::Core`; from G6 `gbm`, `libdrm` | adds Wayland headers | `WaylandSession` and protocol clients; `hyprland/`, `wlroots/`, `kde/` subdirectories |
| `backends/portal` | `src/backends/portal/` | `platform`, `Qt6::DBus` | | XDG desktop portals, `FileManager1`, desktop-environment D-Bus services |
| `backends/x11` | `src/backends/x11/` | `platform`, xcb libraries | | XShm, XComposite, EWMH, XTest, XInput2, XFixes, `XGrabKey` |
| `backends/macos` | `src/backends/macos/` | `platform`, Apple frameworks; from M2 `ml` (interfaces it implements) | ObjC++ `.mm`; Swift in `swift/` | ScreenCaptureKit, `NSPanel`, status item, Carbon hotkeys, pasteboard, Vision, Speech, Translation, Sparkle |
| `media` | `src/media/` | `core`, `render`, FFmpeg, PipeWire, `Qt6::GuiPrivate`; from G5/G6 `platform`, `libva`, `libdrm`, `Qt6::Multimedia`, libimagequant | | recorder, writer, audio, telemetry, decoder, studio compositor, exporter |
| `ml` | `src/ml/` | `core`, `Qt6::Gui`, ONNX Runtime, zxing-cpp, CTranslate2, whisper.cpp, Tesseract (optional); **never** `Qt6::Network` or `net` | | recognition interfaces, Linux engines, model store |
| `net` | `src/net/` | `core`, `Qt6::Network`, `Qt6::NetworkAuth`; from M2 `platform` (`SecretStore`), `ml` (the `Translator` and `ModelFetcher` interfaces it implements), QtKeychain | | every HTTP request: uploaders, web translation client, model downloader, update feed; secret stores; network policy |
| `app` | `src/app/` | grows with M1 to every module above | adds `app/` | composition root: dispatcher, controller, capability registry, tray, hotkey service, control server, signal bridge |
| `app/main` | `src/app/main.cpp` | `ariadshot_app` | | the `ariadshot-daemon` executable; thin `main.cpp` only |
| `cli` | `src/cli/` | nothing from Qt; the C++ standard library and POSIX | | the `ariadshot` command-line client |

Edges marked "from Mn" or "from Gn" are expected additions that the architecture requires but that no code needs yet.
Each is added to the `ALLOWED` list of the module's `ariadshot_add_module(…)` call in the pull request that brings its
first consumer, by the manager role, and this table is the approval for it. Any edge not listed here needs an ADR.

**Why these boundaries.** `core` is QtCore-only so models, stores and algorithms are testable without a display and
reusable by tools. `render` and `ui` are QtGui-only so the canvas, chrome and renderer cannot depend on the window that
shows them; that is what lets gate G1 swap the overlay host for the cost of one adapter. Platform code is confined to
`backends/` and `hosts/` so desktop differences are always visible as an interface and a capability.

## 2. Enforcement

1. **Direct-edge rule per target.** At configure time a deferred check reads each module's `LINK_LIBRARIES` and
   `INTERFACE_LINK_LIBRARIES` and requires every direct entry to be in that module's "may link" column, or
   `AriadShot::options`.
2. **Reachability rule for the three portable modules.** For `core`, `render` and `ui` the check walks the transitive
   closure and fails when a forbidden target is reachable: for `core` any Qt target other than `Qt6::Core`, plus any
   platform library; for `render` and `ui` `Qt6::Widgets`, `Qt6::Quick`, `Qt6::GuiPrivate` and platform libraries.
   Allowed transitive paths (`render → core → Qt6::Core`) pass.
3. **Normalisation.** Aliases resolve through `ALIASED_TARGET`; imported targets compare by name; `$<LINK_ONLY:x>` and
   `$<BUILD_INTERFACE:x>` are unwrapped. Qt's plugin-import expression (a condition carrying Qt's
   `$<BOOL:QT_IS_PLUGIN_GENEX>` marker that selects a plugin or its `_init` object library), which Qt's target
   finalizer adds for static plugins such as the Apple permission plugins, counts as an edge to the Qt module the
   plugin extends (its `QT_MODULE`), so the rules apply to that module; a target that is not a Qt plugin fails. Any
   other generator expression fails the check (fail closed); plain library paths and flags are rejected in module link
   properties.
4. **Include rule.** `scripts/check-architecture.sh` checks `#include` lines per directory, which catches a header that
   no link shows (for example a public header of an allowed module that pulls in a forbidden one). It also rejects
   desktop-name checks (`XDG_CURRENT_DESKTOP`, compositor names) outside `backends/`.
5. **Fixtures.** `tests/cmake/arch-*` and `tests/scripts/arch-include-cases/` prove the checker fails for the right
   reason (its own message, matched with `PASS_REGULAR_EXPRESSION`) and accepts allowed transitive paths.

## 3. Cross-cutting conventions

| Concern | Rule |
| :--- | :--- |
| Namespaces | `ariadshot::<module>` (`ariadshot::core`, `ariadshot::render`, …); backends use `ariadshot::backends::<platform>` |
| Files | `PascalCase.h/.cpp/.mm`, one primary class per file, named after it; include root `src/` (`#include "core/BuildInfo.h"`) |
| Fallible APIs | return `ariadshot::Expected<T, E>` (an alias for vendored `tl::expected`, replaced by `std::expected` when the project moves to C++23). `E` is a module error enum (`core::StoreError`, `platform::CaptureError`, …) with a `QString describe(E)` for logs. No exceptions across module boundaries or the event loop |
| User-facing failures | follow MacShot's surfaces (error pill, dialog, toast); never silent. The error pill text comes from the MacShot catalogue where MacShot has a string |
| Logging | one `Q_LOGGING_CATEGORY` per module, `ariadshot.<module>` (`ariadshot.backends.wayland`, …). Never log pixels, clipboard contents, secrets, tokens or file contents; paths only at debug level |
| Units | canvas geometry in points as `QPointF`/`QRectF` in the canonical canvas space (top-left origin, y down); device pixels only at the edges (capture buffers, canonical images, encoders). Conversions live in `core/geometry/CanvasSpace` |
| Time | monotonic time as `std::chrono::nanoseconds` from a steady clock; media timestamps in memory as rational `{int64 value, int32 timescale}` (`core::MediaTime`); persisted encodings of time and wall-clock dates are defined per format in [06 §1](06-annotation-model-and-formats.md) |
| Identifiers | `QUuid` (v4) for sessions, annotations, groups, history items and takes; persisted in the braceless lowercase form |
| Colours | sRGB values (`QColor::Rgb` spec, alpha as a separate component) in memory and in every format; never archived dynamic colours |
| Strings | every user-visible string through `tr()`; sentences with `%1` arguments, never concatenation |
| Threads | each class header states its thread affinity (`// Thread: GUI`, `// Thread: WaylandSession`, `// Thread: any`) |
| Ported constants | named `constexpr` values in one header per subsystem (`kPascalCase`), commented with the behaviour and `macshot/<path>:<line>@b4d4f3a`; defaults come only from the settings registry |

## 4. `core` — models, algorithms, stores

**Responsibility.** Everything that needs neither pixels nor a display: the annotation, edit-state and video-project
models; geometry; non-pixel algorithms; settings, history and session stores; file formats and their codecs; filename
formatting and atomic publishing. `core` is the module that tests "against MacShot, not against ourselves" most
directly: its tests assert ledger values.

| Directory | Contents | MacShot counterpart |
| :--- | :--- | :--- |
| `core/geometry/` | `CanvasSpace` (canvas ↔ view ↔ pixel conversions), `Geometry` (Catmull-Rom, arc length, fitted dashes, Ramanujan perimeters, hit sampling), `Xorshift32` (bit-exact), `Rotation` | geometry helpers spread through `macshot/Model/Annotation.swift` and `macshot/UI/Tools/` |
| `core/annotation/` | `ToolKind`, `Annotation`, `AnnotationDocument`, `UndoStack`, `AnnotationCodec`, `SavedCaptureLimits` | `macshot/Model/Annotation.swift`, `AnnotationCodable.swift`, `LenientDecoding.swift`, `SavedCaptureValidation.swift` |
| `core/edit/` | `CaptureEditState` (effects and beautify parameters) and its normalisation | `macshot/Model/CaptureEditState.swift` |
| `core/settings/` | `SettingsRegistry`, `SettingsStore`, `SettingsPortability`, `Accelerator` | `macshot/Services/SettingsPortability.swift`, settings reads across the app |
| `core/history/` | `HistoryStore`, `HistoryIndex`, `HistoryRevision`, `HistoryCleanup` | `macshot/Services/HistoryStorage.swift`, `ScreenshotHistory.swift`, `HistoryFileCleanup.swift` |
| `core/files/` | `AtomicPublish`, `FilenameFormatter`, `FilenameSanitizer`, `ScratchDirectory`, `LaunchCleanup` | `macshot/Services/AtomicMediaSave.swift`, `FilenameFormatter.swift`, `FilenameSanitizer.swift`, `TmpScratchDirectory.swift`, `LaunchCleanup.swift` |
| `core/session/` | `SessionStore` (take folders, status file), `RecordingLifecycle`, `RecordingClock` | `macshot/Capture/RecordingSessionStore.swift`, `RecordingLifecycle.swift`, `RecordingClock.swift` |
| `core/video/` | `VideoProject` and segments, `TimeMap`, `AutoZoomPlanner`, `CursorSpring`, `KeystrokeTimeline`, `TelemetryCodec` | `macshot/Model/Video*.swift`, `macshot/Capture/VideoTimelineMapping.swift`, `CursorMotion.swift`, `KeystrokeTimeline.swift`, `CursorTelemetry.swift` |
| `core/recognition/` | value types shared by ML producers and consumers: `TextObservation`, `DetectedRegion`, `QrResult`; `PiiRedactionPlanner` | `macshot/Services/PIIRedactionPlanner.swift`, `AutoRedactor.swift` (patterns) |
| `core/alignment/` | alignment snapping (5 pt to selection and annotation min/mid/max) | `macshot/UI/Overlay/OverlayView.swift` (alignment snap engine) |

Pixel-reading algorithms (boundary snap index, auto-measure edge scan, colour sampler, opaque-content scan, scroll
frame registration) are not in `core`: they read `QImage` data and therefore live in the audited pixel module of
`render` (§5).

Interface sketch (models and stores are specified in [06](06-annotation-model-and-formats.md) and
[07](07-storage-history-settings.md)):

```cpp
namespace ariadshot::core {

enum class ToolKind : int {            // raw values are persisted; they match MacShot's Annotation tool enum
    Pencil = 0, Line = 1, Arrow = 2, Rectangle = 3, FilledRectangle = 4, Ellipse = 5, Marker = 6, Text = 7,
    Number = 8, Pixelate = 9, Blur = 10, Measure = 11, Loupe = 12, Select = 13, TranslateOverlay = 14,
    Crop = 15, ColorSampler = 16, Stamp = 17, Highlight = 18,
};

class Annotation;                      // value type with an identity (QUuid); clone() copies with a new identity
class AnnotationDocument;              // ordered annotations + group IDs + number counter

struct UndoEntry {                      // MacShot's four entry kinds
    enum class Kind { Added, Deleted, ImageTransform, PropertyChange };
    // payload per kind: annotation, index, previous image references, annotation offsets, property snapshot
};

class SettingsRegistry {               // one typed descriptor per key, generated defaults, import validation
public:
    struct Key { QString name; QMetaType type; QVariant effectiveDefault; Domain domain; Surface owner; bool offline; };
    const Key* find(QStringView name) const;
};

class SettingsStore {                  // "unset" is distinct from "set to the default"
public:
    std::optional<QVariant> stored(QStringView key) const;
    QVariant effective(QStringView key) const;
    Expected<void, StoreError> set(QStringView key, const QVariant& value);
    Expected<void, StoreError> unset(QStringView key);
};

} // namespace ariadshot::core
```

## 5. `render` — the one pixel source

**Responsibility.** Produce every canonical image and every exported still: annotations, beautify frames, effects,
blur, pixelation, erase, text layout, encoding. Everything here follows the still-image contract of
[03](03-rendering-contracts.md). `render` never shows anything; surfaces display its images.

| Directory | Contents | MacShot counterpart |
| :--- | :--- | :--- |
| `render/` | `CanonicalImage` factory, `CanonicalLayers`, `FinalImageComposer` (region + annotations → effects → beautify) | overlay/editor final render paths, `macshot/Services/HistoryImageSnapshot.swift` |
| `render/text/` | `FontSet` (bundled fonts, fixed logical DPI), `TextEngine` (`QTextDocument` layout in canvas points, glyph-outline stroke) | `macshot/UI/Tools/OutlineTextRenderer.swift`, text drawing in `Annotation.swift` |
| `render/annotation/` | `AnnotationRenderer` and per-tool painters, per-annotation raster caches | drawing code in `macshot/Model/Annotation.swift` and tool handlers |
| `render/beautify/` | `BeautifyRenderer`, `MeshGradient` (3×3 bicubic patches), `Shadow` (calibrated ambient + contact) | `macshot/Services/BeautifyRenderer.swift` |
| `render/effects/` | `Effects` (presets, brightness, contrast, saturation, sharpness), `EffectLut`, `Blur` (separable Gaussian from three box passes), `Pixelate`, `Erase` | `macshot/Services/ImageEffects.swift`, `EffectsMigration.swift` |
| `render/encode/` | `ImageEncoder` (PNG, JPEG, WebP, AVIF, HEIC where available; quality; 1× downscale; straight alpha) | `macshot/Services/ImageEncoder.swift` |
| `render/pixel/` | **the audited pixel module**: `PixelSpan` (bounds-checked `std::span` accessors), `BoundarySnapIndex`, `EdgeScan` (auto-measure), `ColorSampler`, `OpaqueScan`, `ScrollFrameAnalyzer` (SAD, scrollbar width, sticky header, phase correlation) | `macshot/Services/BoundarySnapIndex.swift`, `macshot/Capture/ScrollFrameAnalyzer.swift`, overlay pixel helpers |

Every raw pixel loop in the code base lives in `render/pixel/`. Code elsewhere calls it; it never indexes image memory
itself.

```cpp
namespace ariadshot::render {

// ARGB32_Premultiplied, QColorSpace::SRgb, devicePixelRatio set, transparent fill.
QImage makeCanonicalImage(QSize pixelSize, qreal devicePixelRatio);

struct RenderContext { qreal captureScale; const FontSet* fonts; const QImage* source; /* for censor and loupe */ };

class AnnotationRenderer {
public:
    void paint(QPainter& p, const core::Annotation& a, const RenderContext& ctx) const;
    QRectF paintBounds(const core::Annotation& a, const RenderContext& ctx) const;   // damage and cache extent
};

class FinalImageComposer {             // the confirm pipeline of MacShot: region + annotations, then effects, then beautify
public:
    Expected<QImage, RenderError> compose(const QImage& source, QRect regionPx, const core::AnnotationDocument& doc,
                                          const core::CaptureEditState& edit, const std::optional<QImage>& windowImage);
};

} // namespace ariadshot::render
```

## 6. `platform` — interfaces and the capability vocabulary

**Responsibility.** Declare every desktop-dependent service as an abstract interface, with value types that carry no
platform headers. The implementations live in `backends/` and `hosts/`; `app` chooses them at start-up from probes and
registers what works in the capability registry ([08 §1](08-platform-integration.md)).

The eight interface families are fixed by ADR 0001. `DesktopIntegration` is split into small facets so a backend can
implement only what its desktop offers; the facet split is this specification's and may be refined by the first
implementing pull request.

**Thread and completion contract.** A platform operation that can wait on anything outside the process (a
compositor round trip, a portal dialog, D-Bus, compositor IPC, the keyring, the input helper) is asynchronous. Portal
consent alone can take seconds, and the GUI thread may never block ([01 §7](01-architecture-overview.md)). Every such
operation has one shape:

1. It is called on the GUI thread and returns at once with a `Ticket`; the work runs on the backend's own thread (the
   `WaylandSession` thread, an asynchronous D-Bus call, a worker).
2. Its completion runs **exactly once, on the GUI thread**, queued to the caller's `CallContext::receiver`; if the
   receiver was destroyed, the completion is dropped. Operations that report per output (stills) call their per-output
   callback exactly once for each requested output.
3. The caller's session ID (the capture session of SH-13 or the recorder session of CR-07) travels in the
   `CallContext` and back in the completion. The caller discards a completion whose session is no longer current; a
   backend may drop the work early when it learns the session ended.
4. `Ticket::cancel()` makes the completion arrive with the interface's `Cancelled` error, unless it already ran.
   Cancellation never leaves a portal dialog, capture session or data-control source open.

Only state the backend already holds in memory (its capabilities, `SecretStore::kind()`) is read synchronously.
`SurfaceHost` and `SurfaceContent` are GUI-thread objects by nature: `create()` returns at once and the host reports
mapping through `HostedSurface`.

```cpp
namespace ariadshot::platform {

class Ticket { public: void cancel(); };                     // cancellation handle of one pending operation
struct CallContext { QPointer<QObject> receiver; QUuid session; };
template <class T, class E> using Completion = std::function<void(Expected<T, E>)>;   // runs once, GUI thread

struct OutputId { QString name; };     // matched across Qt and AriadShot's own Wayland connection by wl_output.name

class CaptureBackend {                  // stills and screen streams
public:
    struct StillRequest { std::vector<OutputId> outputs; bool paintCursor; };   // pointer output first
    struct StillFrame  { OutputId output; QImage image; qreal scale; QRect logicalGeometry; };
    // onFrame runs once per requested output, pointer output first, as each frame arrives.
    virtual Ticket captureStills(const StillRequest&, const CallContext&, Completion<StillFrame, CaptureError> onFrame) = 0;
    virtual Ticket captureWindow(const WindowRef&, const CallContext&, Completion<QImage, CaptureError>) = 0;   // real alpha
    virtual Ticket openStream(const StreamRequest&, const CallContext&, Completion<StreamHandle, CaptureError>) = 0;
};

class SurfaceContent {                  // implemented by ui::ViewRoot; platform never includes ui headers
public:
    virtual std::vector<const QImage*> presentationLayers() const = 0;   // canonical layers + chrome, draw order
    virtual QRegion takeDamage() = 0;
    virtual void dispatch(const QEvent&) = 0;          // pointer, tablet, key, input method, focus, wheel
    virtual std::optional<Qt::CursorShape> cursor() const = 0;           // nullopt = hidden (drawn by the canvas)
    virtual KeyboardInteractivity keyboard() const = 0;                  // None | OnDemand | Exclusive
};

class SurfaceHost {                     // maps platform surfaces and forwards events to their content
public:
    enum class Role { Overlay, Thumbnail, Pin, Toast, HistoryPanel, Countdown, RecordingHud, WebcamBubble,
                      ScrollPreview };
    virtual std::unique_ptr<HostedSurface> create(Role, OutputId, SurfaceContent&) = 0;   // GUI thread, non-blocking
};

class WindowDirectory {                 // window snapping and focus return
public:
    virtual Ticket windowsFrontToBack(OutputId, const CallContext&, Completion<std::vector<WindowInfo>, WindowError>) = 0;
    virtual Ticket activate(const WindowRef&, const CallContext&, Completion<void, WindowError>) = 0;
    virtual Ticket focusedWindow(const CallContext&, Completion<std::optional<WindowInfo>, WindowError>) = 0;  // title + ref
};

class ClipboardService {                // focused copy and background copy (data-control / selection owner)
public:
    virtual Ticket setContent(ClipboardContent, CopyContext, const CallContext&, Completion<void, ClipboardError>) = 0;
    virtual Ticket content(ContentKinds, const CallContext&, Completion<ClipboardContent, ClipboardError>) = 0;
};

class InputSynth {                      // pointer warp, virtual pointer wheel bursts (auto-scroll)
public:
    virtual Ticket warpPointer(OutputId, QPointF, const CallContext&, Completion<void, InputError>) = 0;
    virtual Ticket scrollLines(int lines, const CallContext&, Completion<void, InputError>) = 0;
};

class InputTelemetry {                  // cursor position and shape, clicks, keys for recordings and live overlays
public:
    // The sink receives events on the media telemetry thread, never on the GUI thread.
    virtual Ticket start(TelemetryKinds, TelemetrySink&, const CallContext&,
                         Completion<std::unique_ptr<TelemetrySession>, InputError>) = 0;
};

// DesktopIntegration facets; every operation that waits on the desktop follows the contract above.
class GlobalShortcuts;   // register named actions, request chords, read back triggers, activation signal
class StatusItem;        // tray / NSStatusItem: icon, menu, click-to-stop during recording
class Autostart;         // XDG autostart entry / SMAppService
class FileManager;       // reveal items (FileManager1.ShowItems, else OpenURI OpenDirectory)
class UrlOpener;         // open URL or file, "Open With" chooser (OpenURI ask: true)
class WallpaperSource;   // current wallpaper where readable (studio background)

class SecretStore {                     // keyring with a visible file fallback; a keyring may prompt
public:
    virtual Ticket read(QString key, const CallContext&, Completion<std::optional<QByteArray>, SecretError>) = 0;
    virtual Ticket write(QString key, QByteArray value, const CallContext&, Completion<void, SecretError>) = 0;
    virtual Ticket remove(QString key, const CallContext&, Completion<void, SecretError>) = 0;
    virtual StorageKind kind() const = 0;   // Keyring | File (degraded, shown in the Uploads tab); cached
};

} // namespace ariadshot::platform
```

Streams: `StreamHandle` is either a ready `ScreenStream` object (image-copy session loop, XShm, ScreenCaptureKit) that
delivers `VideoFrame`s, or a `PipeWireNode` (file descriptor and node ID from a ScreenCast portal session) that `media`
consumes directly with PipeWire. A `VideoFrame` carries one of a DMA-BUF descriptor (fds, strides, offsets, modifier,
DRM format), a CPU buffer, or a macOS `IOSurface` reference, plus the presentation timestamp, damage and optional
cursor metadata ([05 §3](05-recording-and-studio.md)).

Audio: on Linux `media` opens PipeWire streams for the sink monitor and the microphone itself; on macOS system audio
arrives with the ScreenCaptureKit stream and the microphone through a `platform::MicrophoneSource` implemented by the
macOS backend. `media` hides the difference behind `media::AudioInput`.

## 7. `ui` — host-agnostic view objects

**Responsibility.** The ported MacShot views: overlay and editor canvases, toolbars, options rows, resolution box,
popovers, chrome menus, tooltips, helper cards and error pills, tool handlers, the canvas text control, and the content
of every overlay-class panel (thumbnail, pin, toast, history panel, countdown, recording HUD, webcam bubble, scroll HUD
and preview, keystroke pill, click ring). No QtWidgets: a view object is shown only through a surface host or an
ordinary window that embeds a `ViewRoot`.

```cpp
namespace ariadshot::ui {

class ViewObject {                      // ports one MacShot NSView subclass
public:
    virtual QRectF geometry() const;                   // in parent points
    virtual void paint(QPainter&) = 0;                 // chrome layer or canonical layer, never both
    virtual ViewObject* hitTest(QPointF);              // pass-through rules per MacShot (OV-36)
    virtual void hoverChanged(bool);
    virtual std::optional<Qt::CursorShape> cursorAt(QPointF) const;
    virtual QString toolTip() const;
    virtual QAccessibleInterface* accessible();        // every chrome view object is accessible
    void update(QRectF damage);                        // accumulates damage for the host
};

class ViewRoot : public platform::SurfaceContent {    // one per hosted surface or embedding window
public:
    void dispatch(const QEvent&) override;             // routes to the hit view object, focus and IME owner
    QRegion takeDamage() override;
    std::vector<const QImage*> presentationLayers() const override;
};

class OverlayDelegate;                  // MacShot's OverlayViewDelegate output API (29 callbacks), ported 1:1
class OverlayCanvas;                    // ports OverlayView: states idle/selecting/selected, flags, draw order
class EditorCanvas;                     // ports EditorView: overrides OverlayCanvas's 14 extension points
class CanvasTextControl;                // QTextDocument/QTextCursor/QTextLayout; QInputMethodEvent from any host
class ToolHandler;                      // ports AnnotationToolHandler and the per-tool handlers

} // namespace ariadshot::ui
```

`OverlayDelegate` keeps MacShot's callback set and names (translated to C++ naming): finish selection, selection
changed, cancel, confirm, save, save as, pin, OCR, quick save, file save, upload, share, remove background, enter
recording mode, start recording, stop recording, detach, scroll capture, stop and cancel scroll capture, toggle
auto-scroll, request accessibility permission, request input monitoring permission, begin selection, remote selection
changed and finished, snap mode changed, add capture, restore last selection (`macshot/UI/Overlay/OverlayView.swift:6-41`).
Permission requests map to the capability registry's "how to enable" route on Linux. Recognition results reach the
canvas as `core::TextObservation` lists through the delegate; `ui` never links `ml`.

The 14 `EditorCanvas` extension points are MacShot's (`macshot/UI/Overlay/OverlayView.swift:1650-1709`): chrome
cursor, point-in-selection, editor background, selection image clipping, selection border, resolution box, point
adjustment, editor transform, selection resize, new selection, capture draw rect, highlight dim bounds, detach, top
chrome click. Behaviour of the overlay and editor is specified in [04](04-capture-and-overlay.md).

## 8. `hosts` — surface hosts

**Responsibility.** Implement `platform::SurfaceHost`: map one surface per role and output, present a `ViewRoot`'s
layers with damage, forward pointer, keyboard, tablet and input-method events, set cursor shapes and keyboard
interactivity. The host is the only host-specific piece of an overlay; canvas and chrome are unchanged when it is
swapped.

| Host | Class | Where used |
| :--- | :--- | :--- |
| H1 | `LayerShellWidgetHost` (`QWidget` + `layer-shell-qt`) | default for layer-shell compositors |
| H2 | `LayerShellQuickHost` (`QQuickWindow` + `layer-shell-qt`, images as textures, no QML) | if G1 selects it |
| H3 | `WaylandLayerHost` (own layer-shell client: `wl_shm`, `wp_viewporter`, fractional scale, xkbcommon, `text-input-v3`) | if both Qt hosts fail G1 steps 1 or 2 |
| macOS | `PanelHost` (`NSPanel`, non-activating, level 257/258, all Spaces, full-screen auxiliary) | macOS |
| X11 | `OverrideRedirectHost` | X11 |
| GNOME | `FullscreenToplevelHost` (frameless fullscreen `xdg_toplevel` per output with an activation token) | GNOME Wayland and any compositor without layer-shell |

Which host runs is decided at start-up from the capability registry, never from the desktop name. The host contract
and the G1 acceptance steps are in [04 §5](04-capture-and-overlay.md).

## 9. `windows` — ordinary windows

| Class | Ports | Parity rows |
| :--- | :--- | :--- |
| `EditorWindow` (frame around an embedded `EditorCanvas` view root, top bar, zoom menu, dirty tracking) | `macshot/UI/Editor/DetachedEditorWindowController.swift`, `EditorTopBarView.swift`, `CenteringClipView.swift` | ED-01 to ED-04 |
| `SettingsWindow` (seven tabs; Linux adds the Desktop Integration page) | `macshot/UI/Windows/SettingsWindowController.swift` | ST-01 to ST-07 |
| `OcrResultWindow` | `macshot/UI/Windows/OCRResultController.swift` | ED-10 |
| `AudioMergeDialog` | `macshot/UI/Windows/AudioMergeController.swift` | CR-13 |
| `ExportProgressWindow` | `macshot/UI/Windows/MediaExportProgressController.swift` | ED-12 |
| `FirstRunWindow` (macOS onboarding; Linux capability content) | `macshot/UI/Windows/PermissionOnboardingController.swift` | SH-22, DEV-30 |
| `ModelConsentDialog` | none (Linux-only flow) | SH-33 |
| `StudioWindow` (top bar, inspector, `StudioStage` on `QRhiWidget`, transport, timeline) | `macshot/UI/Editor/VideoEditorWindowController.swift`, `macshot/UI/Editor/Video/*` | VE-09 to VE-15 |

Windows never call `net` or `ml`. They emit requests (upload, translate, recognise) that `app` fulfils, and they receive
results as `core` value types. This keeps the dependency list short and makes every network action flow through one
controller that applies the network policy ([10](10-upload-and-network.md)).

## 10. `backends` — platform implementations

| Directory | Implements | Key classes |
| :--- | :--- | :--- |
| `backends/wayland/` | `CaptureBackend`, `ClipboardService`, `InputSynth`, `InputTelemetry` (cursor), `GlobalShortcuts` (Hyprland fallback) | `WaylandSession` (own `wl_display`, own thread), `OutputRegistry`, `ImageCopyCapture`, `ScreencopyCapture`, `DataControlClipboard`, `VirtualPointer`, `CursorSession`; `hyprland/`: `ToplevelExport`, `HyprlandShortcuts`, `HyprlandIpc` (window list, `focuswindow`, binds read-back); `wlroots/`: `SwayIpc`; `kde/`: KWin probes |
| `backends/portal/` | `GlobalShortcuts`, `CaptureBackend` (Screenshot portal stills, ScreenCast streams as `PipeWireNode`), `FileManager`, `UrlOpener`, file chooser | `PortalSession` (request/response handling, restore tokens), `GlobalShortcutsPortal`, `ScreenshotPortal`, `ScreenCastPortal`, `OpenUriPortal`, `FileManager1` |
| `backends/x11/` | `CaptureBackend` (XShm, XComposite), `WindowDirectory` (EWMH), `InputSynth` (XTest), `InputTelemetry` (XInput2, XFixes), `GlobalShortcuts` (`XGrabKey`), `ClipboardService` (selection owner) | `X11Connection`, per-feature classes |
| `backends/macos/` | every interface on macOS, plus the ML interfaces backed by Apple frameworks | `ScreenCaptureKitBackend`, `PanelSupport`, `StatusItemMac`, `CarbonShortcuts`, `PasteboardClipboard`, `LoginItem`, `VisionOcr`, `VisionMasks`, `SpeechCaptions`, `swift/TranslationShim`, `SparkleUpdater` |

The optional Linux input helper (M3) delivers click and key events to `InputTelemetry`. Its process and privilege
model is decided by an ADR when it is built; its privacy contract is fixed now in [11 §6](11-security-privacy.md).

Thread rules for `WaylandSession`: it owns its `wl_display` and event queue, dispatches only on its own thread, never
touches a Qt GUI object, and posts results to the GUI thread through queued signals. Qt's Wayland connection keeps only
what is tied to Qt's surfaces: layer surfaces, text input, cursor shape and `wp_pointer_warp_v1`
([04 §3](04-capture-and-overlay.md)).

## 11. `media` — recorder and studio

| Directory | Contents | MacShot counterpart |
| :--- | :--- | :--- |
| `media/record/` | `Recorder` (lifecycle, clock, heartbeat, watchdogs), `RecordingConfiguration`, `SampleValidation` | `macshot/Capture/RecordingEngine.swift`, `RecordingConfiguration.swift`, `RecordingSampleValidation.swift` |
| `media/write/` | `Mp4Writer` (fragmented MP4, states `writing` … `recovering`), `BitratePlan`, `Remuxer` | `macshot/Capture/MP4WriterSession.swift`, `VideoEncodingSettings.swift` |
| `media/sources/` | `VideoSource` (adapters over `platform::ScreenStream`, `PipeWireVideoSource`), `AudioInput` (`PipeWireAudioInput`, platform microphone adapter), `CameraSource` | `macshot/Capture/ScreenCaptureManager.swift`, `MicrophoneCapture.swift`, `VideoCameraRecorder.swift` |
| `media/gpu/` | VA-API import and processing (crop, RGB → NV12 BT.709), VideoToolbox bridge | none (AVFoundation handles this in MacShot) |
| `media/audio/` | `AudioTrackMixer` (merge dialog maths), resampling | `macshot/Capture/AudioTrackMixer.swift` |
| `media/telemetry/` | `TelemetryWriter`, `TelemetryReader` | `macshot/Capture/CursorTelemetryRecorder.swift`, `CursorTelemetry.swift` |
| `media/gif/` | `GifWriter` (libimagequant palettes, delta rectangles, held duplicates) | `macshot/Capture/GIFEncoder.swift`, `GIFExporter.swift` |
| `media/studio/` | `FrameSceneBuilder`, `StudioCompositor` (QRhi passes), `Decoder` + texture cache, `Exporter`, `CaptionTranscriptionJob` | `macshot/Capture/VideoScene*.swift`, `VideoComposition*.swift`, `VideoExportJob.swift`, `VideoTranscoder.swift`, `VideoCaptionTranscriber.swift` |

```cpp
namespace ariadshot::media {

class Recorder : public QObject {       // Thread: GUI for control; work on media threads
    Q_OBJECT
public:
    enum class State { Idle, Preparing, Recording, Paused, Stopping };
    Expected<void, RecordError> start(const RecordingConfiguration&);
    void pause(); void resume(); void stop();
Q_SIGNALS:
    void stateChanged(State);
    void failed(RecordError);           // no frames, audio overload, format change, append failure, watchdog, disk
    void finished(core::TakeRef);
};

struct FrameScene;                      // immutable; built only by FrameSceneBuilder::build(projectRevision, time)

class StudioCompositor {                // one pass list and shader set; preview and export both call render()
public:
    virtual void render(const FrameScene&, QRhiRenderTarget*, QRhiCommandBuffer*) = 0;
};

} // namespace ariadshot::media
```

The recording and studio contracts are in [05](05-recording-and-studio.md) and [03 §3](03-rendering-contracts.md).

## 12. `ml` — recognition services

`ml` declares the recognition interfaces and implements them on Linux; the macOS implementations are Apple-framework
shims in `backends/macos` that implement the same interfaces. Engines are called only on the ML worker thread; `app`
queues requests to it with a cancellation token and the capture session ID, and results return to the GUI thread as
`core` value types ([09 §2](09-ml-services.md)).

**No network in `ml`.** Every HTTP request in AriadShot is made by `net` and passes `net::NetworkPolicy`. `ml`
therefore declares the two network-backed interfaces it needs and never links `Qt6::Network` or `net`: `Translator`
(implemented by `net::GoogleTranslator`) and `ModelFetcher` (implemented by `net::ModelDownloader`). `app` constructs
both implementations and injects them, so the graph stays acyclic: `net → ml`, never `ml → net`.

```cpp
namespace ariadshot::ml {

class OcrEngine        { public: virtual Expected<std::vector<core::TextObservation>, MlError> recognize(const QImage&, OcrOptions) = 0; };
class QrDecoder        { public: virtual Expected<std::vector<core::QrResult>, MlError> decode(const QImage&) = 0; };
class Translator       { public: virtual void translate(QStringList lines, LanguagePair, TranslateCallback) = 0; };
class SubjectMasker    { public: virtual Expected<QImage, MlError> foregroundMask(const QImage&) = 0; };   // alpha mask
class RegionDetector   { public: virtual Expected<std::vector<core::DetectedRegion>, MlError> detect(const QImage&, RegionKind) = 0; };  // faces, people
class CaptionTranscriber { public: virtual void transcribe(AudioRef, CaptionOptions, CaptionCallback) = 0; };
class ModelFetcher {   // implemented by net::ModelDownloader; the only way ml obtains bytes from the network
public:
    // Streams url to a temporary file in the models directory; completes on the GUI thread; cancellable.
    virtual platform::Ticket fetch(QUrl url, QString tempPath, const platform::CallContext&, FetchProgress,
                                   platform::Completion<void, MlError>) = 0;
};
class ModelStore;      // catalogue, consent state, install layout, SHA-256 verification, sideload, deletion;
                       // downloads only through the injected ModelFetcher

} // namespace ariadshot::ml
```

Engines, models, the catalogue format and the download flow are in [09](09-ml-services.md).

## 13. `net` — uploads, translation, updates

| Class | Purpose |
| :--- | :--- |
| `NetworkPolicy` | the single gate every request passes: allowed only for user-initiated upload, translation, update check and consented model download; uploads, account sign-in and model downloads are always denied in the offline build |
| `ModelDownloader` | implements `ml::ModelFetcher`: streams a consented model download to the file `ml::ModelStore` names, with progress and cancellation; `ModelStore` verifies the SHA-256 and installs ([09 §13](09-ml-services.md)) |
| `Uploader` (interface), `ImgbbUploader`, `GoogleDriveUploader`, `S3Uploader` (own SigV4) | uploads with streamed bodies and progress ([10](10-upload-and-network.md)) |
| `GoogleTranslator` | implements `ml::Translator` over the web endpoint MacShot uses |
| `UpdateFeed` | Linux check-only release feed; the macOS Sparkle path is in `backends/macos` |
| `KeychainSecretStore`, `FileSecretStore` | `platform::SecretStore` over QtKeychain, and the 0600 file fallback ([11 §4](11-security-privacy.md)) |

The offline build compiles `net` without uploaders; the web translator and update checks stay, as in MacShot
([10 §6](10-upload-and-network.md#6-offline-build)).

## 14. `app` — composition root

| Class | Responsibility | Parity rows |
| :--- | :--- | :--- |
| `Application` | builds the object graph from probes, owns the module singletons, runs the launch sequence | SH-02 |
| `CapabilityRegistry` | implements the capability vocabulary of `platform`, resolves it at start-up and on display changes | [08 §1](08-platform-integration.md) |
| `CommandDispatcher` | one entry for hotkeys, tray, CLI, URL scheme and file opens; the launch queue and the capture queue | SH-03, SH-04 |
| `AppController` | capture gate, session IDs, focus return, own-window hide and deferred restoration with a generation token, output routing (confirm, quick save, save, pin, OCR, upload, record, scroll) | SH-13, SH-17 to SH-21, SH-32 |
| `HotkeyService` | slot table, registration through `GlobalShortcuts`, menu label read-back | SH-09, SH-10 |
| `TrayController` | tray or status item and the status menu | SH-07, SH-08 |
| `ControlServer` | single instance and the line-delimited JSON control socket | SH-01, [08 §5](08-platform-integration.md) |
| `SignalBridge` | SIGINT/SIGTERM through a self-pipe into a clean quit | — |

`app` is a STATIC library (`ariadshot_app`) so it is testable without the executable. `main.cpp` constructs
`QApplication`, parses `--help`/`--version`, and hands control to `Application`.

## 15. `cli` — the Qt-free client

The `ariadshot` executable links no Qt library, so shell-triggered captures do not pay Qt start-up. It maps its
arguments to one control-socket request, starts the daemon when no socket answers, prints the daemon's message on
failure and exits with its code. The command set, protocol and exit codes are in [08 §5](08-platform-integration.md).

## 16. Tests and tools

| Directory | Contents |
| :--- | :--- |
| `tests/support/` | `ImageCompare` (exact and CIEDE2000 classes), test-environment helpers; linked by tests only |
| `tests/unit/<module>/`, `tests/golden/`, `tests/trace/`, `tests/integration/`, `tests/fuzz/`, `tests/cmake/`, `tests/scripts/` | see [12](12-testing-strategy.md) |
| `tools/ledger/` | parity-ledger generator and reporter; reads MacShot source from a path argument |
| `tools/imgdiff/` | command-line front end of `ImageCompare` for golden reviews |
| `tools/bench/` | benchmark harness for the performance budgets |

Tools may link any module; no product module links a tool.
