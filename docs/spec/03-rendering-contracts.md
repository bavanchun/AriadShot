<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 03. Rendering Contracts

Part of the [AriadShot technical specification](README.md). Decisions and their rationale:
[ADR 0001](../adr/0001-architecture-and-stack.md). Module ownership: `render/` for stills and the studio's shaders in
`media/studio/` ([02](02-modules-and-interfaces.md)).

## 1. Scope and principles

AriadShot has exactly two renderers, and each one is bound by a contract that is tested, not assumed:

- the **still renderer** in `render/`: `QPainter` raster on `QImage`, producing every canonical image that any surface
  shows and every still that leaves the application (§2);
- the **studio compositor** in `media/studio/`: one `QRhi` pass list and shader set that both the studio stage and the
  studio export run (§3).

"Preview equals export" is therefore a rendering contract with comparison tests, not a property that holds by
construction. A widget backing store and a `QImage` can differ in device pixel ratio, font and emoji fallback,
antialiasing, colour space and presentation scaling; a preview `QRhi` and an export `QRhi` can differ in device, target
format, frame timing and colour conversion. Each contract below removes one of those freedoms.

Rules that apply to both renderers:

1. **Surfaces never re-render content.** A surface host, an ordinary window or the studio stage displays images the
   renderer produced; it may transform them with the one documented presentation filter, nothing else.
2. **Pixel loops live in one audited place.** Every raw pixel loop is in `render/pixel/` (stills) or in a shader
   (studio), behind bounds-checked `std::span` accessors ([02 §5](02-modules-and-interfaces.md)).
3. **Routines Qt lacks are AriadShot's own** (§4), and each is specified against the MacShot routine it replaces.
4. **The rendering-contract tests run in CI on every change** that touches `render/`, `media/`, `ui/` or a surface
   host ([12](12-testing-strategy.md)).

Only one outcome of these contracts can reopen the stack decision: gate G3 failing in a way specific to `QPainter`, or
gate G5 failing on both Vulkan and Metal while an equivalent wgpu spike passes (ADR 0001).

## 2. Still-image contract

### 2.1 The canonical image

Every still layer is rendered once into a canonical `QImage` made by `render::makeCanonicalImage`:

| Property | Value |
| :--- | :--- |
| Format | `QImage::Format_ARGB32_Premultiplied` |
| Colour space | `QColorSpace::SRgb` attached to the image |
| Pixel size | the captured pixel size of the region, or its 1× downscale when `downscaleRetina` is on (§7) |
| Device pixel ratio | the capture scale of the output the region came from (for a region spanning outputs, see [04 §4](04-capture-and-overlay.md), mixed-DPR rule, DEV-34) |
| Initial content | fully transparent |
| Logical DPI for layout | 72 dpi, so 1 pt equals 1 canvas unit as in AppKit (§2.3) |

A capture region spanning outputs with different scales is stitched into one canonical source image by the rule in
[04 §4](04-capture-and-overlay.md); the renderer never mixes scales inside one canonical image.

### 2.2 Canonical layers

A capture under edit is a stack of canonical images, each with the same pixel size and device pixel ratio:

| Layer | Content | Exported |
| :--- | :--- | :--- |
| `source` | the frozen capture pixels, as delivered by the capture backend, converted to the canonical format | yes (as the base of the composition) |
| `committed` | all committed annotations, in document order | yes |
| `inProgress` | the annotation being drawn, and the live dim of a highlight being dragged | no; its content is committed into `committed` |
| `editingText` | the text box under edit by the canvas text control ([06 §3](06-annotation-model-and-formats.md)) | no; committed text lands in `committed` |
| `chrome` | toolbars, options row, popovers, menus, tooltips, handles, guides, badges, pills, and the tool previews (loupe, crop, auto-measure, stamp hover, drawing cursor dot) | **never** |

Live interaction repaints only damaged rectangles of the affected layer, as MacShot's cached-layer design does
(`macshot/UI/Overlay/OverlayView.swift:1817`). The draw order that composes these layers on screen is in §6.

### 2.3 Fixed inputs

The output of the still renderer is a function of these inputs and nothing else:

1. **Model and state.** The annotation document, the capture edit state ([06](06-annotation-model-and-formats.md)) and
   the source pixels.
2. **A fixed font set**, owned by `render::FontSet`:
   - On Linux the bundled Inter (the default for MacShot's system font, DEV-03) and Noto Color Emoji (DEV-04) are
     registered explicitly through `QFontDatabase::addApplicationFont`, and every text font is built with an explicit
     family list (`QFont::setFamilies`) that ends in the bundled emoji family. Nothing is left to fontconfig
     substitution. A family chosen in the font picker (AN-21) is used when installed; when it is not, the run falls
     back to the default family, and the cached text image of an unchanged annotation is kept ([06 §3](06-annotation-model-and-formats.md)).
   - On macOS the system font and Apple Color Emoji, exactly as MacShot.
3. **Text rasterisation.** Hinting off (`QFont::PreferNoHinting`), grayscale antialiasing only
   (`QFont::NoSubpixelAntialias`), text laid out in canvas points on a paint device of 72 logical dpi, independent of any
   screen. The text layout (`QTextDocument`/`QTextLayout`) is the one the canvas text control uses while editing, so
   live editing and export share one layout engine under zoom and rotation.
4. **Image interpolation per draw**, as MacShot chooses it: nearest-neighbour for the pixelate upscale (§4.2), smooth
   (bilinear or better) for stamps, the loupe source and the beautify background image.
5. **No screen state.** Nothing reads the screen's DPI, colour profile, font settings or the Qt platform theme. Tests
   enforce this by running under a neutral Qt environment ([12](12-testing-strategy.md)).

The mechanism that pins the logical DPI (a 72-dpi paint device set on the document layout, `QImage` dots-per-metre, or
explicit pixel sizes) is chosen by the G3 implementation; the contract is that a 20 pt MacShot font renders 20 canvas
units tall on every platform, which G3 tests.

### 2.4 Presentation never re-renders

The overlay, editor, thumbnail, pin and history cards display canonical layers through the view transform:

- at 1:1 on an output whose scale equals the image's device pixel ratio, presentation is a blit;
- otherwise one documented filter is used: **nearest-neighbour at integer zoom ≥ 1, bilinear everywhere else**.

A host that cannot present at the image's native pixel size (for example a fractional-scale output without viewporter
support) scales with the same filter. Thumbnails, history cards and pin windows downscale from the canonical final
image with the same rule; they never draw annotations themselves.

**Large outputs.** If gate G1 shows that a 4K or zoomed editor misses the frame budget, presentation may use a
downscaled copy of the source layer, as MacShot's display-preview helper does (at most 1400 px on the long side,
medium-quality interpolation, `macshot/Capture/ScreenCaptureManager.swift:315`; at the pinned commit only its tests call
it). This is a presentation-only mitigation (G1 outcome O9): it never changes exported pixels, and the presentation test
of §2.7 then compares against the downscaled reference.

### 2.5 Final image composition

Confirming a capture produces the final image in MacShot's order (OV-28):

1. the region of the `source` layer (or, for a window-snap selection, the separately captured window image with real
   alpha corners), with the committed annotations drawn over it; annotations made on a window snap are drawn onto the
   window image;
2. then the effects (§4.6), applied to that composite;
3. then beautify (§5), which wraps the result.

`render::FinalImageComposer` implements this order ([02 §5](02-modules-and-interfaces.md)). The editable history
revision stores the inputs separately (raw image, annotation document, edit state) so the same composition can be
re-run; the raw image is the region without annotations, or the window image when the selection was a window snap
([07](07-storage-history-settings.md)).

### 2.6 Export conversions

| Output | Conversion | Equality rule |
| :--- | :--- | :--- |
| PNG file | canonical image → `QImage::Format_RGBA8888` (Qt's un-premultiplication is the single specified rounding rule) → PNG with an sRGB tag | the decoded PNG's bytes equal the canonical image after the same conversion (exact class) |
| JPEG, WebP, AVIF, HEIC files | the same RGBA8888 buffer (straight alpha for WebP; alpha flattened where the format has none, as the encoder does) → encoder at the configured quality (default 0.85) | PSNR and SSIM against the canonical image at quality 0.85; thresholds are set at G3 [HYPOTHESIS] |
| Clipboard | the same encoder calls as a file export with the same settings | the clipboard PNG is byte-identical to the PNG file export of the same image; a configured-format entry is byte-identical to that file export |
| History, pin, drag-out, upload | the canonical final image through the same encoders | as the matching file export |

Clipboard flavours follow MacShot (`macshot/Services/ImageEncoder.swift:238-254`): PNG always; the configured format
first when `clipboardIncludesImageFormat` is on and the format is not PNG; TIFF additionally on macOS, where MacShot
always adds it for applications that read only TIFF. A generation counter discards an encode that finished after a newer
copy started (`macshot/Services/ImageEncoder.swift:256-267`).

**Colour.** Canonical images are sRGB. On macOS the capture backend requests sRGB from ScreenCaptureKit, so exports are
sRGB-tagged on every platform; MacShot instead embeds the display profile it captured with. Colour management is a
non-goal (ADR 0001), and corpus images taken on a wide-gamut display are converted to sRGB before comparison
([12 §4](12-testing-strategy.md)).

### 2.7 Tests (gate G3, then CI)

| Test | Compares | Class |
| :--- | :--- | :--- |
| Golden | canonical renders of fixture documents against the MacShot reference corpus | perceptual, ΔE00 ≤ 2 on 99.5 % of pixels outside declared tolerance masks; masks cover font-raster, emoji and preset-look regions on Linux and name their deviation entries |
| Regression golden | canonical renders against AriadShot's own reviewed goldens on the same platform | exact |
| Export | decoded exported files against the canonical image | exact for PNG; PSNR/SSIM for lossy formats |
| Presentation | a test-mode readback of each host's presented buffer (and, in the nested compositor, an image-copy of the output) against the canonical image | exact at integer scale; at fractional scale ΔE00 ≤ 1 on 99.9 % of pixels against the canonical image resampled with the documented filter |

G3 covers three representative tools, a mesh gradient, the calibrated shadow and text with emoji. The metric itself is
defined in [12 §4](12-testing-strategy.md).

## 3. Studio contract

The studio's stage and its export are two views of one function: `FrameScene` → pixels. MacShot achieves this by
running one Core Image renderer for both (`macshot/Capture/VideoSceneRenderer.swift`); AriadShot achieves it with the
rules below, verified by gate G5.

### 3.1 One scene snapshot

An immutable `media::FrameScene` is built by exactly one function, `FrameSceneBuilder::build(projectRevision,
compositionTime)`. It resolves everything a frame needs:

- the source frame and its timestamp (through the time map of cuts, speeds and freezes);
- the camera state (crop, frame placement, zoom and pan, motion-blur sub-sample positions);
- the cursor state (position after spring smoothing, shape, sway, press bounce, click effects);
- the text overlays, captions, keystroke label and webcam frame with their opacities.

Preview and export both render `FrameScene`s and nothing else. The studio pipeline that feeds the builder (decode, time
map, auto-zoom, cursor maths) is specified in [05](05-recording-and-studio.md).

### 3.2 One pass list and shader set

The compositor runs one pass list in MacShot's layer order (VE-08, `macshot/Capture/VideoSceneRenderer.swift:153-230`):

| # | Pass | MacShot stage |
| :-: | :--- | :--- |
| 1 | Content-anchored censors on the source pixels (solid, pixelate with 20 pt blocks, blur with a 30 pt radius) | `:153` |
| 2 | Crop, place the recording in its frame; background (gradient, colour, image, wallpaper) with its blur; rounded-corner mask; two-pass drop shadow; hairline border | `:165` |
| 3 | Camera transform (zoom and pan) over the whole scene, with motion blur accumulated from sub-samples | `:197` |
| 4 | Output-space text overlays, which follow the content through the camera | `:203` |
| 5 | Pointer with sway, press bounce and click effects | `:217` |
| 6 | Screen-fixed overlays in this order: webcam bubble, keystroke label, captions (captions on top) | `:222-230` |
| 7 | Encode to 8-bit sRGB | (implicit in Core Image's output conversion) |

There is one shader set, written in portable GLSL and compiled with `qsb` to SPIR-V, MSL and GLSL. Preview and export
load the same `.qsb` files and record the same passes.

### 3.3 Colour and precision

- Blending happens in an **RGBA16F linear working space**: MacShot's compositor runs Core Image with a linear sRGB
  working colour space (`macshot/Capture/EffectsVideoCompositor.swift:160`). Sources are decoded to linear on sampling.
- The last pass encodes to **8-bit sRGB** into an RGBA8 target. Preview and export use identical target formats for
  this pass; the preview then presents that target on the stage with the documented presentation filter of §2.4.
- **One RGB → YUV path** feeds every encoder: BT.709, limited range, a shader pass producing NV12 for VA-API and
  VideoToolbox, and the identical matrix in the libswscale fallback. The recorder's VA-API processing uses the same
  matrix ([05](05-recording-and-studio.md)).

### 3.4 Timing

- Export renders at the exact rational cadence of the take (`VideoFrameCadence`, which keeps rates such as 30000/1001
  instead of rounding them, `macshot/Capture/VideoFrameCadence.swift:128`).
- Preview renders at the playback clock.
- Equality is defined and tested only at identical composition times; a preview frame shown at time *t* must equal the
  export frame at time *t*.

### 3.5 Graphics API selection

| Platform | Preview (`QRhiWidget`) | Export |
| :--- | :--- | :--- |
| Linux | `QRhiWidget::setApi(QRhiWidget::Api::Vulkan)` set explicitly (the widget otherwise defaults to OpenGL there); OpenGL when Vulkan is unavailable | its own offscreen `QRhi` on the same backend as the preview |
| macOS | Metal | its own offscreen Metal `QRhi` |

Export never borrows the widget's `QRhi`: `QRhiWidget` may recreate its `QRhi` when it moves between offscreen and
visible use. Both Linux backends are tested; the OpenGL fallback must pass the same comparisons as Vulkan.

### 3.6 Tests (gate G5, then CI)

For sampled times in fixture projects:

1. The preview's render at export resolution (a test mode of the stage) against the export's pre-encode render:
   ΔE00 ≤ 1 on 99.9 % of pixels.
2. The export's decoded frame against the pre-encode render: PSNR ≥ 38 dB and SSIM ≥ 0.97 [HYPOTHESIS, set at G5].

Both run on Vulkan and the OpenGL fallback on the reference host, and on Metal once gate G7 provides the Mac
environments.

### 3.7 Pinning

`QRhi` needs `Qt6::GuiPrivate` and has limited compatibility guarantees. The Qt minor version is pinned per release,
the compositor sits behind the `media::StudioCompositor` interface, and shaders stay portable GLSL (risk R7).

## 4. Renderer routines Qt lacks

Each routine is AriadShot's own code in `render/` (or a studio shader where noted) and is specified against the MacShot
routine it replaces. Where MacShot uses an Apple filter whose internals are unpublished, the Linux routine is fitted to
MacShot corpus renders and the difference is registered.

### 4.1 Gaussian blur

- **Algorithm.** A separable Gaussian approximated by three successive box passes per axis, with box widths derived from
  σ by the standard three-box construction, and clamp-to-edge sampling. MacShot clamps with `CIAffineClamp` before
  `CIGaussianBlur` so edges do not darken (`macshot/Model/Annotation.swift:2253-2272`).
- **Censor blur** (AN-09a): σ = max(10, 0.03 · min(w, h)) in pixels of the region.
- **Beautify background blur:** the configured blur (0–50) on the custom background image, cached (§5.4).
- MacShot passes these values as Core Image's `inputRadius`; AriadShot treats that value as σ [HYPOTHESIS], confirmed or
  corrected at G3 by comparing against corpus renders.

### 4.2 Pixelate

AN-09a, `macshot/Model/Annotation.swift:1953-2020`: downscale the region to ⌊w/8⌋ × ⌊h/8⌋ (at least 1 × 1) with low-quality
(area-averaging) interpolation, then to half of that again, then upscale to twice the region's point size with
nearest-neighbour interpolation, which gives crisp blocks. The bake is drawn into the region's rect.

### 4.3 Erase

AN-09a, `macshot/Model/Annotation.swift:2135-2249`: content-aware fill by edge interpolation.

1. Pad the region by 4 pt.
2. For each row, average up to 3 pixels just outside the left and right edges; for each column, the same above and below.
3. Each interior pixel is the mean of the horizontal interpolation (left → right) and the vertical interpolation
   (top → bottom), with alpha 255.

MacShot's comments assume a bottom-left pixel origin while its pixel reader uses a top-left one; the output is a
symmetric average, and AriadShot reproduces the output, not the comments.

### 4.4 Shadows

The calibrated two-shadow system of beautify (SV-01, `macshot/Services/BeautifyRenderer.swift:493-518`), for a shadow
radius *r* > 0 and *t* = clamp(*r* / 100, 0, 1):

| Shadow | Alpha | Downward offset | Blur |
| :--- | :--- | :--- | :--- |
| Ambient | 0.42 + 0.38 *t* | min(4 + 0.35 *r*, 18) | *r* |
| Contact | 0.20 + 0.30 *t* | min(2 + 0.12 *r*, 10) | min(4 + 0.18 *r*, 16) |

At *r* = 0 both are off. The shadow colour is black.

- **Rounded mode casts from the image's own rounded alpha** (MacShot's anti-rim technique,
  `macshot/Services/BeautifyRenderer.swift:529-564`): each pass renders the rounded-clipped image into a transparency
  layer, blurs that layer's alpha as the shadow, and composites it; a final pass draws the crisp image. In AriadShot the
  shadow is the blurred (§4.1) alpha mask of the rounded image, offset and tinted, so no caster rim appears.
- **Window mode casts from the opaque window body** (`macshot/Services/BeautifyRenderer.swift:566-591`): the contact and
  ambient shadows of a white-filled rounded window rect, drawn under the window.
- **Snapped windows** cast both shadows from the window image's own alpha, contact first, then ambient, then the crisp
  image (`macshot/Services/BeautifyRenderer.swift:840-895`).

### 4.5 Gradients

- **Mesh gradients.** The 18 mesh styles are 3 × 3 control grids of points and colours
  (`macshot/Services/BeautifyRenderer.swift:96-322`), rendered by MacShot through SwiftUI's `MeshGradient`. AriadShot
  evaluates each style per pixel with bicubic patch interpolation between the nine control points, on a worker thread,
  and caches the result per style and pixel size. The interpolation is fitted so that corpus renders of every mesh style
  pass the golden class of §2.7; the residual difference, if any, is a deviation entry.
- **Linear gradients.** Colour stops and an angle in degrees (0 = left → right, 90 = bottom → top in MacShot's y-up
  convention). The start and end points are the rect centre ± the direction vector scaled to
  min(max(halfW/|dx|, halfH/|dy|), hypot(halfW, halfH)), extended beyond both ends
  (`macshot/Services/BeautifyRenderer.swift:678-700`). AriadShot converts the angle once into its y-down canvas space.
- Gradients are evaluated in sRGB-encoded space, as Core Graphics does for these gradients; they are not linearised.

### 4.6 Effects

SV-02, `macshot/Services/ImageEffects.swift:1-127`. The configuration is a preset plus four adjustments:

| Parameter | Range | Default |
| :--- | :--- | :--- |
| `preset` | none 0, noir 1, mono 2, sepia 3, chrome 4, fade 5, instant 6, vivid 7 | none |
| `brightness` | −0.5 … 0.5 | 0 |
| `contrast` | 0.5 … 2.0 | 1 |
| `saturation` | 0 … 2 | 1 |
| `sharpness` | 0 … 2 | 0 |

Order of application:

1. The preset. Vivid is a fixed colour adjustment (contrast 1.2, saturation 1.5, brightness 0) and **skips the user
   adjustments of step 2**. Sepia uses intensity 0.8. Noir, mono, chrome, fade and instant are Apple's photo-effect
   presets.
2. Brightness, contrast and saturation, only when any of them is non-neutral.
3. Luminance sharpening with the given sharpness, only when it is above 0.
4. The result is rendered to 8-bit RGBA in the source's colour space.

On macOS the effects run through Core Image, exactly as MacShot. On Linux:

- noir, mono, chrome, fade and instant are 3D LUTs fitted to MacShot corpus renders of a calibration chart (DEV-05);
- sepia, the colour controls and the sharpening are implemented as documented Core Image semantics, and each is
  fitted to corpus renders where the documentation is silent [HYPOTHESIS until M2];
- the LUTs ship as data files under `resources/` with their provenance.

MacShot repairs settings an old build polluted with Vivid's boost (`macshot/Services/EffectsMigration.swift:20-62`).
AriadShot never writes that state, so the check applies only to an imported MacShot settings export
([07](07-storage-history-settings.md)).

## 5. Beautify renderer

SV-01, `macshot/Services/BeautifyRenderer.swift`. Beautify wraps the composited capture in a background.

### 5.1 Configuration

| Field | Range | Default | Notes |
| :--- | :--- | :--- | :--- |
| `mode` | window 0, rounded 1 | window | window adds a synthetic title bar |
| `styleIndex` | index into the style list, taken modulo its length; −1 = custom image | 0 | see §5.2 |
| `padding` | 0 … 1024 when loaded, UI 16 … 96 | 48 | points around the window or image |
| `cornerRadius` | 0 … 1024 when loaded, UI 0 … 30 | 10 | window or image corners |
| `shadowRadius` | 0 … 100 | 20 | §4.4 |
| `backgroundRadius` | 0 … 30 | 8 | outer background corners; see DEV-10 below |
| `isWindowSnap` | bool | false | the source already contains native window chrome |
| custom background | PNG image | none | with `backgroundBlur` 0 … 50 |

Load-time bounds come from `macshot/Model/CaptureEditState.swift:92-104`; the edit-state format is in
[06](06-annotation-model-and-formats.md).

**DEV-10.** MacShot keeps a `beautifyBgRadius` setting (default 8) but constructs every beautify configuration with a
background radius of 0 (`macshot/UI/Overlay/OverlayView.swift:524`, `macshot/Model/CaptureEditState.swift:53`), so
neither its preview nor its export rounds the background. AriadShot honours the setting in preview and export. Corpus
fixtures for beautify therefore set the background radius to 0, and DEV-10 records the difference for the default.

### 5.2 Style catalogue

The list is the 18 mesh styles followed by the 30 linear styles, 48 in all
(`macshot/Services/BeautifyRenderer.swift:93-478`). MacShot includes the mesh styles only on macOS 15 and later, so on
macOS 14 its indices name different styles. AriadShot renders mesh styles on every platform (§4.5), so its index space
is MacShot's macOS 15 index space everywhere, including macOS 14. The colour and point tables are copied into one
header with their MacShot source lines and a `PROVENANCE.md` entry; they are not repeated here.

### 5.3 Modes

| Mode | Canvas | Content |
| :--- | :--- | :--- |
| Window | image + 28 pt title bar, plus padding on every side | background fills the whole canvas; shadow from the window body (§4.4); window body white 0.97; title bar 0.94 with a 0.5 pt separator of 0.82; three traffic lights of radius 6 pt at x = 14, 34, 54 pt from the window's left edge with 0.5 pt rings; content below the title bar; everything clipped to the window's rounded rect (`macshot/Services/BeautifyRenderer.swift:735-833`) |
| Rounded | image plus padding | background; image clipped to its rounded rect with the anti-rim shadows (`:897-933`) |
| Snapped window | image plus padding | background; the window image, whose native corners are transparent, with both shadows cast from its alpha; the preview uses a 10 pt native corner radius (`:840-895`) |

### 5.4 Background

- Gradient styles fill the whole canvas (§4.5).
- A custom image fills the canvas with aspect-fill scaling, centred and clipped, after the configured blur
  (`macshot/Services/BeautifyRenderer.swift:607-676`). The blurred image is cached and recomputed only when the image or
  the blur changes.

### 5.5 Live preview and toolbar animation

- The overlay's beautify preview (OV-25, `macshot/UI/Overlay/OverlayView.swift:3132-3445`) draws the same background,
  shadow and window chrome around the selection in the overlay, then re-draws the canvas-side passes that the preview
  would otherwise cover (in-progress annotation, auto-measure, annotation selection chrome, loupe preview, colour-sampler
  pill, alignment guides, drawing cursor dot, crop preview). In AriadShot the preview is a canonical layer rendered by
  `BeautifyRenderer` itself, so the preview's pixels are the export's pixels at the same scale.
- Toggling beautify animates the toolbar anchor between the selection rect and the expanded rect (padding plus the
  28 pt title bar in window mode) with a 60 Hz timer that adds 0.08 progress per tick and quadratic ease-out
  1 − (1 − *t*)² (`macshot/UI/Overlay/OverlayView.swift:3445-3462`). This timing is a UX-feel budget item
  ([12](12-testing-strategy.md)).

## 6. Overlay draw order and damage

The overlay composes its layers in MacShot's 24-pass order (OV-24, `macshot/UI/Overlay/OverlayView.swift:1713-2297`).
The table assigns each pass to the canonical layer or the chrome layer; that assignment decides what an export can ever
contain.

| # | Pass | Layer | Clip |
| :-: | :--- | :--- | :--- |
| 1 | Frozen capture and the 0.45 black dim outside the selection (editor: background and centred image) | presentation of `source` | none |
| 2 | Window or element snap highlight | chrome | none |
| 3 | Idle helper card or selecting badge | chrome | none |
| 4 | Remote selection border and handles (another output owns the drag) | chrome | remote rect |
| 5 | Selection cut-out (undimmed capture; transparent hole during scroll capture) | presentation of `source` | selection |
| 6–7 | Committed annotations (overlay and editor variants) | `committed` | canvas transform; translate overlays clipped to the selection |
| 8 | In-progress annotation and live highlight dim | `inProgress` | canvas transform |
| 9 | Auto-measure preview | chrome | canvas transform |
| 10 | Crop drag preview | chrome | canvas transform |
| 11 | Loupe preview | chrome | canvas transform |
| 12 | Colour-sampler hex pill | chrome | canvas transform |
| 13 | Annotation selection chrome | chrome | canvas transform |
| 14 | Drawing cursor dot or marker pill | chrome | canvas transform |
| 15 | Alignment guides | chrome | canvas transform |
| 16 | Lasso marquee | chrome | canvas transform |
| 17 | Beautify live preview | canonical beautify preview (§5.5) plus re-drawn chrome | canvas transform |
| 18 | Effects live preview | canonical effects preview | selection |
| 19 | Selection border | chrome | none |
| 20 | Selection handles | chrome | none |
| 21 | Boundary-snap guides | chrome | none |
| 22 | Live text box chrome and handles (the text itself is `editingText`) | chrome | none |
| 23 | Stamp hover preview | chrome | canvas transform |
| 24 | Error pill and button tooltip | chrome | none |

Damage and caching rules:

- Every view object reports damage rectangles; the host presents only the union of damaged rectangles
  ([02 §7](02-modules-and-interfaces.md)).
- Each annotation keeps its own raster cache (text image, censor bake, loupe bake, outline glow), invalidated when the
  annotation's geometry, style or source changes; `committed` is re-composed only over the damaged rect.
- The highlight dim is drawn once per frame for all highlight annotations, over the union of their holes, at the
  **maximum** `dimOpacity` among them (`macshot/Model/Annotation.swift:2085-2129`).
- Mesh gradients and blurred backgrounds are cached per style, size and blur (§4.5, §5.4).

## 7. Encoding

SV-03, `macshot/Services/ImageEncoder.swift`.

| Format | Setting value | Extension | Quality | Availability |
| :--- | :--- | :--- | :--- | :--- |
| PNG | `png` (default) | `png` | lossless | everywhere |
| JPEG | `jpeg` | `jpg` | 0.1 … 1.0 | everywhere |
| HEIC | `heic` | `heic` | 0.1 … 1.0 | macOS; Linux only when a libheif encoder is present, otherwise hidden |
| WebP | `webp` | `webp` | 0.1 … 1.0 | everywhere; at most 16,383 px per side, larger images fail the save with an error (`:150`, `:172`) |
| AVIF | `avif` | `avif` | 0.1 … 1.0 | only where an encoder is present; the format is hidden otherwise, as MacShot gates it (`:83-94`) |

- **Quality** comes from `imageQuality`, clamped to 0.1 … 1.0, default 0.85 when missing or not finite (`:64-69`).
- **1× downscale.** With `downscaleRetina` on and the image larger than its point size in both dimensions, the image is
  rendered down to its point size (width and height each at least 1) with high-quality interpolation before encoding
  (`:112-121`).
- **Straight alpha.** WebP receives un-premultiplied RGBA; MacShot rounds to nearest,
  (*v* · 255 + *a* / 2) / *a* for 0 < *a* < 255 (`:175-187`). AriadShot uses the RGBA8888 conversion of §2.6 for every
  format, and the export test proves the WebP result matches it.
- **Bounded allocation.** Buffer sizes are computed with overflow checks before allocation; a huge capture fails the save,
  never the application (`:150-155`).
- Encoding runs off the GUI thread; the copy budget is W3 in [12](12-testing-strategy.md).

## 8. Performance rules

- Repaint only damaged rectangles; never re-render a whole canonical layer for a pointer move.
- Keep per-annotation raster caches and invalidate them precisely (§6).
- Build the boundary-snap index, mesh gradients, blurs, effects and encodes on workers, cancellable, and deliver results
  by queued signal ([01 §7](01-architecture-overview.md)).
- The drag frame budget is ≤ 16.7 ms p95 for workload W2 on topologies P1, P2 and P3 ([12](12-testing-strategy.md)). If
  it is missed after these rules, presentation moves to textures (host H2) and, for large outputs, to the downscaled
  presentation of §2.4; `QPainter` remains the rasteriser and exported pixels do not change.
