<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 06. Annotation Model and Formats

Part of the [AriadShot technical specification](README.md). This file defines the persisted content formats: the
annotation document, the capture edit state, the studio video project, the recording telemetry log and the take status
file, with the text model and the undo model that produce them. Where these files live and how they are written
atomically is in [07](07-storage-history-settings.md). Module ownership: `core/annotation/`, `core/edit/`,
`core/video/`, `core/session/` ([02 §4](02-modules-and-interfaces.md)).

## 1. Scope and format principles

1. **Own formats, MacShot's semantics (DEV-28).** AriadShot does not read or write MacShot's files byte for byte
   (property lists, Apple reference-date numbers, RTF payloads, the binary `cursor.mstl`). It keeps MacShot's fields,
   defaults, bounds and meaning. Keys keep MacShot's names wherever the value means the same thing; a key is renamed only
   when its encoding changes (for example RTF → text runs). Importing MacShot data is a possible later convenience and is
   not required ([01 §12](01-architecture-overview.md)).
2. **JSON documents**, UTF-8 without a byte-order mark, LF line endings, written with **sorted keys** so identical
   content produces identical bytes (MacShot sorts keys for projects, `macshot/Model/VideoProject.swift:487-491`).
   Binary logs are used only where appends must survive truncation (§8).
3. **Every document is self-describing:** a top-level `format` string and an integer `version` (§10).
4. **Common value encodings** (AriadShot format decision):

   | Value | Encoding |
   | :--- | :--- |
   | Point | `[x, y]` |
   | Rect | `[x, y, width, height]`, width and height ≥ 0 |
   | Colour | `[r, g, b, a]`, sRGB components 0 … 1 |
   | Identifier | UUID string, written lowercase without braces, read case-insensitively |
   | Date | ISO 8601 in UTC with a `Z` suffix (`2026-01-31T12:00:00.000Z`) |
   | Image payload | base64 of a PNG file |
   | Time in a media file | seconds as a double, on the clock stated by the format |

5. **Lenient decoding.** Only the fields a document cannot be interpreted without are required. A missing, `null` or
   wrongly typed optional field takes its default; a field with a value out of bounds is clamped or replaced by its
   fallback as stated per field. Arrays decode element-wise: one bad element costs that element, not the document
   (MacShot's `LenientArrayDecoder`, `macshot/Model/LenientDecoding.swift:33-59`). Unknown keys are ignored.
6. **Caches are never state.** Rendered text images, censor bakes and similar rasters may be stored to preserve
   appearance, but every one of them is regenerable from the state fields, and no behaviour depends on a cache being
   present.
7. **Allocation bounds** (`macshot/Model/SavedCaptureValidation.swift:7-13`) apply at every decode of saved captures:

   | Bound | Value |
   | :--- | :--- |
   | Embedded image | PNG only, one frame, ≤ 128 MiB encoded, ≤ 128 Mi pixels (width × height ≤ 134,217,728) |
   | Rich text payload | ≤ 4 MiB (MacShot's RTF bound, applied to the encoded `textRuns` value and to `text` as UTF-8) |
   | Coordinates | finite and \|v\| ≤ 1,000,000 points |
   | Text raster | width × height × 4 ≤ 128 Mi bytes before a text image is rendered |

   The WebP encoder's 16,383 px side limit is an export limit, not a document limit ([03 §7](03-rendering-contracts.md)).
8. **Canvas coordinates.** Annotation geometry is stored in the canonical canvas space of the image it annotates:
   points, origin at the image's top-left corner, y growing downward ([02 §3](02-modules-and-interfaces.md)). MacShot
   stores image-local points with a bottom-left origin (`macshot/UI/Overlay/OverlayWindowController.swift:486-500`); the
   conversion y′ = H − y (and for rects y′ = H − (y + height)) happens only in an importer. Rotation angles are radians,
   positive clockwise on screen, which is MacShot's value negated.

## 2. Annotation model

AN-01, AN-02, AN-18. `core::Annotation` is a value type with a run-time identity (a `QUuid` assigned on creation and
on decode; not persisted, as in MacShot). `clone()` copies every persisted field and every cache and assigns a new
identity; it does not copy the transient source-image reference used for re-baking.

### 2.1 Tool kinds

Raw values are persisted and match MacShot's `AnnotationTool` (`macshot/Model/Annotation.swift:3-23`):

| Value | Kind | Notes |
| :-: | :--- | :--- |
| 0 | pencil | freeform path with optional pressures |
| 1 | line | |
| 2 | arrow | |
| 3 | rectangle | |
| 4 | filledRectangle | |
| 5 | ellipse | |
| 6 | marker | |
| 7 | text | §3 |
| 8 | number | |
| 9 | pixelate | the censor tool; the mode is `censorMode` |
| 10 | blur | legacy censor kind; decodes as a censor with `censorMode` = blur |
| 11 | measure | |
| 12 | loupe | |
| 13 | select | a tool, never persisted |
| 14 | translateOverlay | not movable, never persisted (the flattened image keeps it) |
| 15 | crop | a tool, never persisted |
| 16 | colorSampler | a tool, never persisted |
| 17 | stamp | |
| 18 | highlight | |

A document may contain any movable kind (MacShot persists only annotations whose `isMovable` is true,
`macshot/Model/Annotation.swift:363-370`). A reader accepts every known raw value and drops an annotation whose `tool` is
missing or unknown.

### 2.2 Enumerations

| Field | Values (persisted integers) | Fallback | Source |
| :--- | :--- | :--- | :--- |
| `lineStyle` | solid 0, dashed 1, dotted 2 | solid | `macshot/Model/Annotation.swift:25-29` |
| `rectFillStyle` | stroke 0, strokeAndFill 1, fill 2 | stroke | `:74-78` |
| `numberFormat` | decimal 0, roman 1, alpha 2, alphaLower 3 | decimal | `:80-85` |
| `censorMode` | pixelate 0, blur 1, solid 2, erase 3 | pixelate | `:115-120` |
| `arrowStyle` | single 0, thick 1, double 2, open 3, tail 4, sketchy 5 | single | `:131-138` |
| `textAlignment` | `"left"`, `"center"`, `"right"` (strings; MacShot stores an AppKit alignment integer) | `"left"` | `macshot/Model/AnnotationCodable.swift:29` |

### 2.3 Persisted fields

Keys follow MacShot's `CodableAnnotation` (`macshot/Model/AnnotationCodable.swift:7-71`). Load rules follow its
`fromCodable` (`:237-348`). "Drop" means the whole annotation is discarded.

| Key | Type | Default | Load rule |
| :--- | :--- | :--- | :--- |
| `tool` | int | — (required) | unknown value → drop |
| `startX`, `startY`, `endX`, `endY` | number | 0 | the start or end point invalid (non-finite or \|v\| > 10⁶) → drop |
| `colorRGBA` | colour | `[1, 0, 0, 1]` | fewer than 4 finite values → red; components clamped to 0 … 1 |
| `strokeWidth` | number | 3 | clamped to 0 … 1024; non-finite → 3 |
| `text` | string | absent | UTF-8 longer than 4 MiB → drop |
| `textRuns` | array of runs (§3.1) | absent | encoded value larger than 4 MiB or undecodable → drop |
| `fontSize` | number | 20 | clamped to 1 … 4096; non-finite → 20 |
| `isBold`, `isItalic`, `isUnderline`, `isStrikethrough` | bool | false | |
| `textDrawRect` | rect | absent (zero rect) | invalid → zero rect |
| `textBgColorRGBA` | colour | absent (no background pill) | |
| `textOutlineColorRGBA` | colour | absent (no outline pill) | |
| `textGlyphStrokeColorRGBA` | colour | absent (no glyph outline) | |
| `textAlignment` | string (§2.2) | `"left"` | unknown → left |
| `fontFamilyName` | string | absent (the default family) | |
| `textImagePNG` | image payload | absent | cache (§2.4); present but invalid → drop |
| `number` | int | absent | |
| `numberFormat` | int (§2.2) | 0 | unknown → decimal |
| `points` | array of points | absent | invalid points are skipped individually |
| `pressures` | array of numbers, parallel to `points` | absent (uniform width) | each clamped to 0 … 1, missing or non-finite → 1; an entry is dropped together with its skipped point, so pressures never shift onto another segment |
| `controlPointXY` | point | absent | invalid → absent (legacy single bend of line and arrow) |
| `anchorPoints` | array of points | absent | invalid points skipped; ≥ 3 points switch the line, arrow or measure to the multi-anchor path |
| `rotation` | number (radians) | 0 | clamped to ±10⁶; only rectangle, filledRectangle, ellipse, stamp, text and number rotate (`macshot/Model/Annotation.swift:172-178`) |
| `rectCornerRadius` | number | 0 | clamped to 0 … 1024 (the UI offers 0 … 30) |
| `lineStyle` | int (§2.2) | 0 | unknown → solid |
| `arrowStyle` | int (§2.2) | 0 | unknown → single |
| `arrowReversed` | bool | false | head at the start instead of the end |
| `rectFillStyle` | int (§2.2) | 0 | unknown → stroke |
| `outlineColorRGBA` | colour | absent (no outline) | shape, line, arrow and loupe outline colour |
| `stampImagePNG` | image payload | absent | content, not a cache; invalid → drop |
| `isCaptureStamp` | bool | false | written only when true (an image added with Add Capture) |
| `bakedBlurPNG` | image payload | absent | censor cache (§2.4); never written for the loupe; invalid → drop |
| `loupeMagnification` | number | 2 | clamped to 0.1 … 100 at decode; the UI offers 1.1 … 6 and a source drag reaches 12 (AN-17b) |
| `loupeSourceRect` | rect | absent | present = the rooted two-circle loupe |
| `loupeOutlineEnabled` | bool | false | the ring uses `outlineColorRGBA` |
| `measureInPoints` | bool | false | pt instead of px labels |
| `censorMode` | int (§2.2) | 0 | unknown → pixelate |
| `groupID` | identifier | absent | invalid → absent |
| `randomSeed` | uint32 | 0 | 0 or absent → a fresh seed in 1 … 2³² − 1 at decode, as MacShot does for old captures |
| `dimOpacity` | number | 0.55 | ≤ 0 or non-finite → 0.55; above 1 → 1 |

Fields that exist in memory but are not persisted: the identity, the transient source image used for baking and its
bounds, the cached selection glow, and MacShot's legacy `isRounded` flag, which `rectCornerRadius` replaced.

### 2.4 Caches in the document

| Cache | Regenerated from | Used while |
| :--- | :--- | :--- |
| `textImagePNG` | `textRuns` (or `text` and the style fields) through the text engine | the annotation's text and style are unchanged; any text or style change re-renders it (`macshot/Model/Annotation.swift:1671`) |
| `bakedBlurPNG` | the source pixels under the annotation, `censorMode` and colour | the annotation has not moved or resized and the source is unchanged; a move or resize clears the bake and re-bakes from the current source (`macshot/Model/Annotation.swift:510-513`) |
| loupe bake | the source pixels, magnification and source rect | never stored; always re-baked from the source |

Writers store the text image and the censor bake, as MacShot does, so a restored document shows exactly what was
exported until an annotation changes, including text whose font is not installed on the reading machine. Pasted
annotations are offset (§2.5), so their censor bakes are redone against the destination capture. A reader that cannot
use a cache regenerates it.

### 2.5 Semantics that constrain persistence

- **Number badges** (AN-08): the next number is the maximum existing number plus one, or the start value when there is
  none. The in-memory counter is not persisted; it is recomputed on load. Undoing the addition of a number decrements the
  counter.
- **Sketchy arrows** (AN-03) use the persisted `randomSeed` with the bit-exact `xorshift32` of `core/geometry/`, so the
  same arrow renders identically after every load.
- **Highlight** annotations share one dim: the maximum `dimOpacity` among them ([03 §6](03-rendering-contracts.md)).
- **Paste and duplicate** offset the new annotations by (+15, +15) canvas points (MacShot's (+15, −15) in its y-up
  space, `macshot/UI/Overlay/OverlayView.swift:9472`, `:9496`). Group rules are in §4.3.

## 3. Text annotations and the canvas text control

AN-07. The attributed text runs are the source of truth for a text annotation; the rendered image is a cache. Live
editing, measurement and export use the same `QTextDocument` layout, so the text a user edits is pixel-identical to the
text that is exported ([03 §2](03-rendering-contracts.md)).

### 3.1 Text runs

`textRuns` replaces MacShot's RTF payload (AriadShot format decision). Each run is a maximal stretch of text with
uniform attributes:

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `text` | string | — (required) | the run's characters; paragraphs are separated by `\n` |
| `fontFamily` | string or `null` | `null` | `null` = the default family (MacShot's "System") |
| `fontSize` | number | the annotation's `fontSize` | points, clamped to 1 … 4096 |
| `bold`, `italic`, `underline`, `strikethrough` | bool | false | underline and strikethrough are single lines |
| `color` | colour | the annotation's `colorRGBA` | glyph fill |
| `outlineColor` | colour or `null` | `null` | the glyph outline drawn **outside** the fill (MacShot's `macshotOutlineColor`) |
| `outlineWidth` | number or `null` | `null` | `null` = automatic, max(1, 0.09 × font size) (`macshot/UI/Tools/OutlineTextRenderer.swift:28-35`) |
| `alignment` | `"left"`, `"center"`, `"right"` | the annotation's `textAlignment` | applies to the paragraph that contains the run's first character; runs of one paragraph agree, and a reader takes the first run's value |

These are exactly the attributes MacShot's text editor sets: font family, size and traits, foreground colour,
underline, strikethrough, paragraph alignment and the outside glyph outline
(`macshot/UI/Tools/TextEditingController.swift:129-251`, `macshot/UI/Tools/OutlineTextRenderer.swift:18-21`).

Rules:

- The concatenated run texts equal `text`. Writers store both; `text` serves search, previews and accessibility.
- When `textRuns` is absent, a reader builds one run from `text` and the annotation-level style fields, as MacShot
  draws a plain `text` when there is no attributed string.
- The annotation-level fields (`fontSize`, `isBold`, `isItalic`, `isUnderline`, `isStrikethrough`, `fontFamilyName`,
  `textAlignment`, `textGlyphStrokeColorRGBA`) hold the controller's current style. Changing the style of a committed
  text annotation re-applies it over the whole text, re-renders the cache, keeps the width and the **top edge**, and sets
  the height to max(old height, ⌈layout height⌉ + 8) (`macshot/Model/Annotation.swift:1671-1744`).
- The background pill (`textBgColorRGBA`, the draw rect grown by 4 pt, radius 4) and the outline pill
  (`textOutlineColorRGBA`, width 2, drawn only when no glyph outline exists) are annotation-level
  (`macshot/Model/Annotation.swift:1645-1666`).

### 3.2 The canvas text control

`ui::CanvasTextControl` ports MacShot's text editing controller and scoped text view
(`macshot/UI/Tools/TextEditingController.swift`, `macshot/UI/Tools/ScopedUndoTextView.swift`). It is built on the
public `QTextDocument`, `QTextCursor` and `QTextLayout` APIs and draws into the `editingText` canonical layer; it is not
an embedded `QTextEdit`, because only an own control guarantees the export layout under zoom and rotation.

| Behaviour | Rule | MacShot source |
| :--- | :--- | :--- |
| Open, new text | a click on empty canvas with the Text tool; frame 200 × max(28, fontSize + 12) pt below the click point | `TextEditingController.swift:256-266` |
| Open, re-edit | a double-click on a text annotation; the controller takes the annotation's style and opens at its `textDrawRect` | `OverlayView.swift:8652-8662` |
| Inset | 4 pt on every side | `TextEditingController.swift:295` |
| Enter, Shift+Enter | insert a newline; no key combination commits | `OverlayView.swift:10256-10258` |
| Esc | cancels; a cancelled re-edit restores the original annotation unchanged | `OverlayView.swift:10261` |
| Commit | a click outside the box (not on a text-formatting control), a double-click, a tool switch, toggling beautify, entering recording, and the copy or paste fallbacks below | `OverlayView.swift:8873-8880` |
| Copy / paste / select all inside the box | with a text selection, copy copies text; without one, copy commits and copies the annotation; paste of an annotation payload commits and pastes annotations; plain text pastes into the box | `OverlayView.swift:9095-9120` |
| Live resize | height = layout height + 2 × inset, at least max(28, fontSize + 12); the top edge stays fixed; the width does not grow by itself | `TextEditingController.swift:449-466` |
| Commit geometry | empty text creates nothing; otherwise height max(max(28, fontSize + 12), ⌈h⌉ + 2 × inset) and width max(⌈w⌉ + 2 × inset, 20), shrunk to the text, top-left edge fixed | `TextEditingController.swift:355-425` |
| Style edits | font size ± clamps to 8 … 200 and stores the `textFontSize` default; bold, italic, underline and strikethrough apply to the selection or, without one, to all text; alignment applies per paragraph; colour applies live | `ToolOptionsRowView.swift:1784-1805`, `TextEditingController.swift:122-251` |
| Typing undo | scoped to the control: the platform undo chord inside the box undoes typing only, never the canvas; the typing history is discarded when the box closes | `ScopedUndoTextView.swift:9-17` |

**Input methods.** The control receives `QInputMethodEvent`s from whichever host shows it (layer-shell surface,
`NSPanel`, ordinary window) and answers `inputMethodQuery` for the cursor rectangle, surrounding text, cursor and anchor
positions, font and hints, with rectangles mapped to host coordinates so candidate windows appear next to the caret.
The pre-edit string is laid out inline by the same text layout and underlined per the input method's attributes; the
commit string is inserted with the current typing attributes. Gate G1 step 4 tests Vietnamese Telex and a CJK engine
through this path ([14 §2](14-gates-and-milestones.md)); where a named input-method and compositor
combination fails, text entry moves to a pop-up and then the editor window, disclosed as DEV-21
([04 §5](04-capture-and-overlay.md), [08](08-platform-integration.md)). Behaviour that stays the same in the fallback:
Enter inserts a newline, clicking outside commits, Esc cancels, typing undo is scoped, and the committed result is the
same text annotation.

**Accessibility.** The control exposes an editable-text accessible interface with the text, the caret and the
selection.

### 3.3 Re-edit and upstream defects

MacShot removes a re-edited annotation from the document without an undo entry and appends a new annotation on commit
(`macshot/UI/Tools/TextEditingController.swift:256-350`). AriadShot keeps the visible behaviour and fixes the undo
defects:

- **Re-edit, then commit with text (DEV-07).** The edited annotation moves to the top of the z-order, as in MacShot. The
  change is one `PropertyChange` undo entry whose snapshot includes the text runs, `text`, the text cache, the draw rect
  and the previous index, so undo restores the original text at its original position.
- **Re-edit, then commit empty (DEV-39).** The annotation is removed, as in MacShot, and the removal is one `Deleted`
  entry with the previous index, so undo restores the original.
- **New text, commit empty.** Nothing is created and no undo entry is made, as in MacShot.

## 4. Undo model

OV-32, `macshot/UI/Overlay/OverlayView.swift:44-65`, `:9553-9670`.

### 4.1 Entries

| Kind | Payload | Undo | Redo |
| :--- | :--- | :--- | :--- |
| `Added` | the annotation | remove it; if it has a group ID, keep undoing while the top entry is `Added` with the same group ID | re-append, replaying the same group |
| `Deleted` | the annotation and its index | re-insert at min(index, count) | remove again |
| `PropertyChange` | the annotation and a snapshot of its previous state | swap current and snapshot | swap back |
| `ImageTransform` | the previous source image, the previous window image, the previous canvas rect, and a geometry snapshot of every annotation the transform moved | restore the images, the canvas rect **and the annotation geometry** (DEV-08) | re-apply |

A `PropertyChange` snapshot restores style and geometry (start, end, points, control point, anchors, draw rect,
rotation) and re-bakes censors and loupes, as MacShot's `copyProperties` does
(`macshot/Model/Annotation.swift:288-332`). AriadShot adds the text content and the previous index for the text
re-edit of §3.3; MacShot's snapshot does not restore text content or stamp images.

### 4.2 Stack rules

- Any new entry clears the redo stack.
- Undoing an `Added` number decrements the number counter; undoing an `Added` translate overlay turns the translate
  state off (`macshot/UI/Overlay/OverlayView.swift:9553-9601`).
- `ImageTransform` covers flips, add capture with canvas expansion, invert and crop commit (OV-33). MacShot records
  annotation offsets only for canvas expansion and ignores them on undo (`:3804`); AriadShot records and restores the
  geometry for every transform (DEV-08).
- The typing history of the canvas text control is separate and never enters this stack (§3.2).

### 4.3 Group IDs

- One auto-redaction run (AN-10) assigns one group ID to all annotations it adds; one translate run (AN-15) does the
  same; both undo and redo as one step.
- Duplicate assigns a **new** group ID only when several annotations are duplicated at once, and a single duplicate
  gets none; a duplicate never inherits its source's group ID (`macshot/UI/Overlay/OverlayView.swift:9483-9500`).
- Paste keeps the group IDs encoded in the pasted payload (`:9462-9480`).

## 5. The annotation document

The file `annotations.json` of a history revision ([07](07-storage-history-settings.md)) and the annotation clipboard
payload share one format:

```json
{
  "format": "ariadshot-annotations",
  "version": 1,
  "annotations": [ { "tool": 2, "startX": 40, "startY": 32, "endX": 180, "endY": 96, "colorRGBA": [1, 0.23, 0.19, 1],
                     "strokeWidth": 4, "arrowStyle": 0, "randomSeed": 2718281828 } ]
}
```

- `annotations` holds the document's movable annotations in z-order (first = bottom), with geometry relative to the
  raw image of the revision. MacShot writes a bare array (`macshot/Model/AnnotationCodable.swift:373-393`); the envelope
  is an AriadShot format decision (§10).
- **Two decode modes.** *Lenient* (paste, previews): element-wise, bad annotations are skipped; an empty result counts
  as no payload. *All-or-nothing* (re-editing raw pixels): any annotation that fails to decode rejects the whole
  document, and the editor falls back to the flattened capture. This keeps a censor that cannot be restored from being
  silently dropped over unredacted raw pixels (`macshot/Model/AnnotationCodable.swift:380-387`).
- **Clipboard type.** The payload is offered as `application/x-ariadshot-annotations+json` on Linux and as the uniform
  type `io.github.bavanchun.ariadshot.annotations` on macOS (MacShot uses `com.sw33tlie.macshot.annotations`,
  `macshot/UI/Overlay/OverlayView.swift:9451`). Copying annotations puts only this payload on the clipboard, as MacShot
  does.

## 6. Capture edit state

AN-19, `macshot/Model/CaptureEditState.swift`. The non-destructive post-processing of a capture: effects and beautify
parameters. The file `edit.json` of a history revision uses this format; it is written only when effects are not the
identity or beautify is on (`macshot/Model/CaptureEditState.swift:35-36`).

```json
{ "format": "ariadshot-edit-state", "version": 1, "beautifyEnabled": true, "beautifyModeRaw": 0, "beautifyStyleIndex": 3 }
```

| Key | Type | Default | Load rule |
| :--- | :--- | :--- | :--- |
| `effectsPresetRaw` | int | 0 | unknown → none (presets in [03 §4.6](03-rendering-contracts.md)) |
| `effectsBrightness` | number | 0 | clamped to −0.5 … 0.5; non-finite → 0 |
| `effectsContrast` | number | 1 | clamped to 0.5 … 2; non-finite → 1 |
| `effectsSaturation` | number | 1 | clamped to 0 … 2; non-finite → 1 |
| `effectsSharpness` | number | 0 | clamped to 0 … 2; non-finite → 0 |
| `beautifyEnabled` | bool | false | |
| `beautifyModeRaw` | int | 0 | window 0, rounded 1; unknown → window |
| `beautifyStyleIndex` | int | 0 | index into the 48-style list, taken modulo its length; −1 = custom image ([03 §5.2](03-rendering-contracts.md)) |
| `beautifyPadding` | number | 48 | clamped to 0 … 1024 |
| `beautifyCornerRadius` | number | 10 | clamped to 0 … 1024 |
| `beautifyShadowRadius` | number | 20 | clamped to 0 … 100 |
| `beautifyBackgroundBlur` | number | 0 | clamped to 0 … 50 |
| `beautifyBackgroundRadius` | number | 8 | clamped to 0 … 30; AriadShot addition for DEV-10, absent in MacShot, whose renderer uses 0 |
| `beautifyIsWindowSnap` | bool | false | the raw image is a window capture with real alpha |
| `customBeautifyBackgroundPNG` | image payload | absent | written only when `beautifyStyleIndex` is −1; invalid → the edit state is unusable (below) |

Normalisation runs on every load and before every apply (`macshot/Model/CaptureEditState.swift:92-104`). Legacy
padding and radius values beyond today's slider ranges are kept inside the load bounds. If the edit state cannot be
decoded, or the custom background cannot be reconstructed, re-editing falls back to the flattened capture, as MacShot's
restore contract does ([07](07-storage-history-settings.md)).

## 7. Studio video project

VE-01, `macshot/Model/VideoProject.swift`. The editable state of one video: trims, crop, look and segments. The file is
`<movie base name>.project.json` beside a take, or in a per-file folder for videos outside the recording library
([07](07-storage-history-settings.md)). A project applies to a source only if its `sourceDuration` is within 0.05 s of
the source's duration; otherwise a fresh project is created
(`macshot/UI/Editor/Video/VideoEditorDocument.swift:116-120`).

### 7.1 Top level

| Key | Type | Default | Load rule |
| :--- | :--- | :--- | :--- |
| `format`, `version` | string, int | `"ariadshot-video-project"`, 1 | §10 |
| `sourceDuration` | number (s) | — (required, > 0) | not finite or ≤ 0 → the project is unusable |
| `trimStart`, `trimEnd` | number (s) | 0, `sourceDuration` | clamped into 0 … `sourceDuration` with start ≤ end; a range shorter than 0.1 s resets to the whole source |
| `muted` | bool | false | |
| `crop` | rect, normalised | `[0, 0, 1, 1]` | non-finite → full frame; origin clamped to 0 … 0.95, size at least 0.05 and inside the frame |
| `look` | object (§7.2) | defaults | |
| `zooms`, `censors`, `texts`, `cuts`, `speeds`, `freezes`, `captions` | arrays (§7.3) | empty | element-wise lenient; then sanitised: a segment must satisfy 0 ≤ start < end, start < `sourceDuration`, finite, and its end is clamped to `sourceDuration`; a freeze needs 0 ≤ `atTime` < `sourceDuration` (`macshot/Model/VideoProject.swift:468-484`) |

All normalised geometry is relative to the source frame after orientation, origin top-left. All segment times are
seconds on the untrimmed source clock; export clips them to the trim range.

### 7.2 Look

| Object | Keys (default; load bounds) |
| :--- | :--- |
| `frame` | `enabled` (false), `background` (below), `aspect` (`"auto"`; one of `auto`, `wide16x9`, `vertical9x16`, `square`, `classic4x3`, `portrait3x4`, `portrait4x5`, `wide16x10`), `padding` (0.08 of the short side; 0 … 0.35), `cornerRadius` (14 pt at a 1080 px reference height; 0 … 64), `shadow` (0.6; 0 … 1), `border` (false) |
| `frame.background` | `kind` (`"gradient"`; `gradient`, `color`, `image`, `wallpaper`), `gradientID` (`"linear-0"`; stable `mesh-N` or `linear-N` identity), `color` (`[0.11, 0.11, 0.13, 1]`), `imageName` (a file name inside the project folder for `image`, an absolute path for `wallpaper`, DEV-19), `blur` (0; 0 … 1, relative to the canvas) |
| `cursor` | `show` (true), `appearance` (`"system"`; `system`, `dot`, `ring`), `size` (1.4; 0.5 … 4), `smoothing` (0.5; 0 … 1), `hideWhenIdle` (false), `idleDelay` (2 s; 0.5 … 10), `hideWhileTyping` (false), `clickEffect` (`"ripple"`; `none`, `ripple`, `spotlight`, `ring`), `clickColor` (`[1, 1, 1, 0.9]`), `pressBounce` (true), `motionBlur` (0.35; 0 … 1), `loopToStart` (false), `sway` (0; 0 … 1) |
| `zoom` | `defaultLevel` (1.8; 1.2 … 5), `transition` (`"smooth"`; `gentle` 1.2 s, `smooth` 0.85 s, `snappy` 0.5 s), `connectZooms` (true; zooms closer than 1.2 s pan directly), `motionBlur` (0.5; 0 … 1), `followDeadZone` (0.45; 0.1 … 0.9) |
| `keystrokes` | `show` (true), `shortcutsOnly` (false), `position` (`"bottomCenter"`), `size` (1; 0.5 … 2.5), `lightAppearance` (false) |
| `camera` | `show` (true), `shape` (`"circle"`; `circle`, `roundedSquare`, `roundedRect`, `vertical`), `size` (0.24 of the short side; 0.08 … 0.6), `position` (`"bottomRight"`), `mirror` (true), `shrinkOnZoom` (true), `shadow` (true) |
| `captions` | `show` (true), `fontSize` (44 pt at 1080 px; 16 … 120), `position` (`"bottomCenter"`), `background` (true), `maxWords` (7; 2 … 20) |

Positions are the nine anchors `topLeft`, `topCenter`, `topRight`, `middleLeft`, `center`, `middleRight`, `bottomLeft`,
`bottomCenter`, `bottomRight`. Values and bounds come from `macshot/Model/VideoProject.swift:56-292`. A new take's
first project uses the remembered look, or the recording default (frame enabled, the catalogue's default gradient);
a non-take video starts from the plain default look ([05](05-recording-and-studio.md)).

### 7.3 Segments

Every segment has an `id` (identifier; a fresh one when missing). VE-06:

| Array | Keys (default; bounds) | Source |
| :--- | :--- | :--- |
| `zooms` | `startTime`, `endTime`; `zoomLevel` (2; 1.2 … 5); `center` (point, `[0.5, 0.5]`, clamped to 0 … 1); `fadeIn`, `fadeOut` (0.35 s); `followsCursor` (false); `isAutomatic` (false). Minimum duration 0.3 s; fades are capped so a plateau remains | `macshot/Model/VideoZoomSegment.swift:14-90` |
| `censors` | `startTime`, `endTime`; `rect` (`[0.35, 0.35, 0.3, 0.3]`, size ≥ 0.02, inside the frame); `style` (`"blur"`; `solid`, `pixelate` with 20 pt blocks, `blur` with a 30 pt radius); `fadeIn`, `fadeOut` (0). Minimum 0.3 s | `macshot/Model/VideoCensorSegment.swift:15-95` |
| `texts` | `startTime`, `endTime`; `rect` (`[0.1, 0.78, 0.8, 0.14]`, size ≥ 0.04); `text` (`""`); `fontSize` (48 pt at 1080 px; 6 … 400); `bold`, `italic` (false); `fontFamily` (`"System"`); `textColor` (white); `bgStyle` (`"none"`; `none`, `solid`, `rounded`); `bgColor` (`[0, 0, 0, 0.7]`); `outlineEnabled` (false); `outlineColor` (black); `outlineWidth` (2; 0 … 40); `alignment` (`"center"`); `fadeIn`, `fadeOut` (0.25 s). Minimum 0.3 s | `macshot/Model/VideoTextSegment.swift:13-205` |
| `cuts` | `startTime`, `endTime`. Minimum 0.1 s; overlapping cuts merge | `macshot/Model/VideoCutSegment.swift:10-110` |
| `speeds` | `startTime`, `endTime`; `speedFactor` (2; 0.25 … 10). Minimum composition duration 0.1 s | `macshot/Model/VideoSpeedSegment.swift:25-70` |
| `freezes` | `atTime`; `holdDuration` (1 s; 0.1 … 30) | `macshot/Model/VideoFreezeSegment.swift:19-51` |
| `captions` | `startTime`, `endTime`, `text` | `macshot/Model/VideoProject.swift:294-314` |

The time map that combines cuts, speeds and freezes is specified in [05](05-recording-and-studio.md).

## 8. Recording telemetry log

CR-15. Pointer, click, key and cursor-shape data recorded next to a take, so the studio can draw a sharp, restyleable
cursor and replay clicks and keystrokes. It replaces MacShot's `cursor.mstl` with the same records
(`macshot/Capture/CursorTelemetry.swift:1-230`); the file is `telemetry.astl` in the take folder.

### 8.1 Layout

Little-endian binary, append-only:

```text
"ASTL" · u32 version (1) · u32 headerLength (≤ 64 KiB) · header JSON · records…
```

Header JSON:

| Key | Type | Meaning |
| :--- | :--- | :--- |
| `sourcePointSize` | `[width, height]` | the recorded region in points; cursor images are sized in points |
| `pixelSize` | `[width, height]` | the recorded video's pixel size |
| `frameRate` | int (default 30) | the configured recording rate |
| `createdAt` | date | |
| `cursorHiddenInVideo` | bool | false: the pixels already contain a cursor, and the studio does not draw a second one by default |
| `overlaysInTelemetry` | bool | click ring and keystroke pill were kept out of the video and are drawn by the studio |
| `clicksRecorded`, `keysRecorded` | bool | AriadShot addition (DEV-24): whether a click or key source existed during the take, so "no clicks happened" and "clicks could not be recorded" are distinguishable |

Records, each a `u8` tag followed by its payload:

| Tag | Record | Payload |
| :-: | :--- | :--- |
| 1 | move | f64 time, f32 x, f32 y |
| 2 | button | f64 time, u8 button (left 0, right 1, other 2), u8 down, f32 x, f32 y |
| 3 | shape | f64 time, u32 shape id |
| 4 | shapeDefinition | u32 id, f32 hotspot x, f32 hotspot y, f32 width, f32 height (points), u32 PNG length (≤ 4 MiB), PNG bytes |
| 5 | key | f64 time, u8 down, u32 keysym, u32 modifiers, u8 text length (≤ 64), UTF-8 text |
| 6 | start | f64 time |
| 7 | pause | f64 time |
| 8 | resume | f64 time, f64 paused duration |

- **Clock.** Times are seconds on the recorder's session clock, derived from the monotonic clock with the start
  anchor and paused duration removed, as for video frames ([05 §2–§3](05-recording-and-studio.md)).
- **Positions** are normalised to the recorded region with a top-left origin; values outside 0 … 1 mean the pointer
  left the region.
- **Keys** carry an XKB keysym and an AriadShot modifier mask (Shift 1, Control 2, Alt/Option 4, Super/Command 8, Caps
  Lock 16) instead of MacShot's macOS key code and AppKit flags (AriadShot format decision); the macOS backend
  translates its key codes into keysyms. The text is truncated to 64 UTF-8 bytes at a character boundary.

### 8.2 Reading

The header must decode or the file is rejected. Records are then read in order until the end of the file, an unknown
tag or a truncated record; everything before that point is kept. A record with a non-finite value is skipped
(`macshot/Capture/CursorTelemetry.swift:160-230`). A crash can also lose records still in the writer's memory buffer;
the durable prefix is bounded by the flush and sync policy below and is reconciled with the recovered movie.

### 8.3 Writing

`media::TelemetryWriter` follows MacShot's sampler (`macshot/Capture/CursorTelemetryRecorder.swift:29-100`):

- on polling backends the pointer is polled every 8 ms; event-driven Wayland sources apply the same unchanged-position
  suppression to delivered events ([05 §8](05-recording-and-studio.md));
- a button change writes a button record instead of a move;
- after more than 20 ms without a record, the first change is preceded by a "hold" move at the resting position,
  timed 8 ms earlier, so playback never drifts across a pause;
- the cursor shape is sampled 15 times per second, each distinct image is written once as a shape definition, and
  later changes write only its id;
- records are buffered and handed to the kernel every 0.5 s or at 64 KiB; the file is synced on the same timer as the
  movie's `fdatasync` (at least once every 2 s, DEV-29), where MacShot syncs every 10 s;
- a write failure closes the log and never stops the take.

Where each record comes from on each desktop (image-copy cursor sessions, portal cursor metadata, XInput2, event taps,
the opt-in helper) is in [05](05-recording-and-studio.md) and [08](08-platform-integration.md).

## 9. Take status file

CR-08. Every take folder holds `session.json` beside the movie. It replaces MacShot's `session.plist`
(`macshot/Capture/RecordingSessionStore.swift:45-53`). The media never depends on decoding it.

```json
{ "format": "ariadshot-take", "version": 1, "createdAt": "2026-01-31T12:00:00.000Z", "filename": "Recording.mp4",
  "status": "recording", "lastSyncedFragment": { "index": 41, "endTime": 42.0, "byteLength": 18874368 } }
```

| Key | Type | Meaning |
| :--- | :--- | :--- |
| `createdAt` | date | when the take started |
| `filename` | string | the movie's file name in this folder: the sanitised configured name, `Recording` when empty, plus `.mp4` (`macshot/Capture/RecordingSessionStore.swift:29-35`) |
| `status` | `"recording"`, `"complete"`, `"interrupted"` | `recording` while the writer is writing, syncing, finalising or publishing; `complete` after publishing; `interrupted` after a failure, a watchdog, or recovery on the next launch |
| `error` | string | optional user-readable reason; never a path or file content |
| `lastSyncedFragment` | object | AriadShot addition: the last fragment made durable by `fdatasync` (`index`, `endTime` in media seconds, `byteLength` of the file up to its end), updated on every sync tick |

The writer states that drive `status` and the recovery procedure that reads `lastSyncedFragment` are in
[05](05-recording-and-studio.md). Other files of a take folder: the movie, `telemetry.astl` (optional), `camera.mp4`
(optional webcam track, `macshot/Capture/VideoCameraRecorder.swift:19`) and the project file (§7).

## 10. Versioning and migration

- `format` names the document type; a reader rejects a document whose `format` it does not expect.
- `version` is an integer that starts at 1. **Additive changes keep the version**: a new optional key with a default is
  readable by older builds (they ignore it) and by newer builds (they default it).
- A change of meaning, encoding or requiredness **bumps the version** and adds a migration from every older version to
  the new one in the codec, with a fixture of the old version under `tests/`.
- A reader that meets a newer `version` than it knows decodes leniently for display but never writes the document back;
  for history it falls back to the flattened capture, and for the studio it opens the video read-only with a notice.
- Migrations are pure functions in `core/` from one document value to the next and are tested against checked-in
  fixtures of every older version.
- A MacShot importer, if one is ever added, is a separate migration source that converts coordinates (§1 item 8), RTF
  to text runs (including MacShot's conversion of legacy centred glyph strokes into outside outlines,
  `macshot/UI/Tools/OutlineTextRenderer.swift:57-78`), Apple reference dates and property lists.

## 11. Census rule

Every persisted field of every format in this file has, before it merges:

1. an encode/decode round-trip test with a non-default value;
2. a default test: the field absent decodes to its default, and `null` or a wrong type does the same;
3. a bounds test for each clamp or fallback in its load rule;
4. for annotations, a `clone()` test that the field is copied, and, where §4.1 says so, a `PropertyChange` restore test.

A census test compares the codec's key list with the fields covered by these tests and fails when a field is added to
a model without them, as MacShot's `AnnotationPersistenceTests` does for its annotation properties
(`macshotTests/AnnotationPersistenceTests.swift:152-252`). Defaults are asserted against the MacShot values cited in the
parity ledger, never against AriadShot's own constants ([12](12-testing-strategy.md)).
