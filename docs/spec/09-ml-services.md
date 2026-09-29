<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 09. ML Services

Part of the [AriadShot technical specification](README.md). This file specifies text recognition, QR decoding,
translation, background removal, face and people detection, PII detection and auto-redaction, captions, and the model
catalogue with its download flow. Interfaces are declared in [02 §12](02-modules-and-interfaces.md); the decisions are
in [ADR 0001](../adr/0001-architecture-and-stack.md).

## 1. Scope and platform split

| Function | macOS | Linux default → fallback | Parity rows |
| :--- | :--- | :--- | :--- |
| OCR (lines with boxes) | Vision text recognition | RapidOCR / PaddleOCR through ONNX Runtime with script routing → Tesseract 5 | SH-19, SH-20, ED-10, AN-06b, AN-09b, AN-10, AN-15 |
| QR | Vision barcodes restricted to QR and Micro QR | zxing-cpp restricted to QR and Micro QR | SH-19, ED-10 |
| Translation | Google web endpoint (default), Apple Translation on macOS 15+ | Google web endpoint (default), optional offline CTranslate2 with OPUS-MT or Argos models | SV-06, SH-20, AN-15, ED-10 |
| Background removal | Vision foreground instance mask (macOS 14+) | BiRefNet-lite 512 ONNX, on demand → U²-Netp | OV-28, ED-04 (remove background) |
| Faces and people | Vision face and human rectangles | YuNet; MobileNetV2-SSD or RT-DETR person boxes | AN-10 |
| PII | shared C++ port of MacShot's patterns and planner | same | AN-10, AN-09b |
| Captions | Speech framework, on device | whisper.cpp with an on-demand model | CR-22, VE-15 |
| Scroll registration | shared C++ (SAD scrollbar and header logic, phase correlation) | same | CR-04 |
| Effect presets | Core Image `CIPhotoEffect*` | fitted 3D LUTs | SV-02 |

- macOS keeps MacShot's zero-download behaviour through the Apple frameworks (DEV-20). Linux gets consented on-demand
  downloads (SH-33, §13).
- Recognisers differ between platforms, so their raw results differ (DEV-06). Everything downstream of a recogniser is
  one shared implementation: PII planning, redaction geometry, the translate-overlay layout, the smart-marker snap,
  scroll stitching and every piece of geometry. A fixture of recognised lines therefore produces identical annotations
  on both platforms.
- Weights licences are checked, not only inference-code licences. CC BY-NC models (RMBG-2.0, NLLB-200) and AGPL
  models (Ultralytics YOLO) are excluded from the catalogue.

## 2. Interfaces and threading

- `ml` declares `OcrEngine`, `QrDecoder`, `Translator`, `SubjectMasker`, `RegionDetector`, `CaptionTranscriber` and
  `ModelStore` ([02 §12](02-modules-and-interfaces.md)). Linux engines live in `src/ml/`; the macOS engines are shims in
  `src/backends/macos/` (Vision, Speech, and the Swift Translation module) that implement the same interfaces.
  `net::GoogleTranslator` implements `Translator` for the web endpoint ([10](10-upload-and-network.md)).
- `ml` makes no network request itself and never links `Qt6::Network` or `net`. It declares `ModelFetcher`, which
  `net::ModelDownloader` implements; `app` injects the downloader into `ModelStore` and the web translator into the
  translation slot, so the module graph stays acyclic (`net → ml`) and every request passes `net::NetworkPolicy`
  ([02 §12](02-modules-and-interfaces.md)).
- `app` owns one engine instance per interface, chosen at start-up and whenever the capability registry reports a
  change (a model was downloaded or deleted). Capability names: `TextRecognition`, `BackgroundRemoval`,
  `FaceAndPeopleDetection`, `OfflineTranslation`, `SystemTranslation`, `Captions`
  ([08 §1](08-platform-integration.md#1-capability-registry)).
- Inference runs on the ML worker, never on the GUI thread. Every call takes a cancellation token; dismissing the
  overlay or closing the window cancels in-flight work, and a late result for a stale capture session is discarded by
  session ID (SH-13).
- Results cross module boundaries only as `core` value types:

```cpp
namespace ariadshot::core {

struct TextObservation {             // one recognised line
    QString text;                    // best candidate
    QPolygonF quad;                  // oriented box in source-image pixels, top-left origin, clockwise from top-left
    QRectF bounds;                   // axis-aligned bounds of quad
    double confidence;               // 0..1, engine-specific
    std::vector<QPolygonF> glyphBoxes;   // one box per character of text, empty when the engine has none
};

struct QrResult { QString payload; QPolygonF quad; };              // payload trimmed, deduplicated per image
struct DetectedRegion { QRectF bounds; RegionKind kind; double confidence; };   // Face | Person

} // namespace ariadshot::core
```

- Images passed to engines are cropped from the canonical source image of the capture ([03 §2](03-rendering-contracts.md))
  in device pixels; callers convert results to canvas points through `core::CanvasSpace`. MacShot crops the selection
  before recognition (`macshot/Services/AutoRedactor.swift:262-273`); AriadShot does the same.

## 3. OCR

### 3.1 macOS

Vision text recognition at level `.accurate` with language correction and automatic language detection; when the
accurate request fails or throws, it retries once at `.fast` (`macshot/Services/VisionOCR.swift:88-131`). Each result
line is the top candidate. Sub-string boxes come from the candidate's range box.

### 3.2 Linux

- **Engine.** RapidOCR / PaddleOCR models through ONNX Runtime (CPU execution provider by default): a language-agnostic
  DBNet detector, a direction classifier, then one recogniser per script.
- **Script routing.** Each recogniser has its own dictionary, so one detection pass is followed by recognition with
  the recogniser for the selected script: CJK (Chinese, Japanese) with the PP-OCR Chinese recogniser, Korean with the
  Korean recogniser, and Latin scripts with the Latin recogniser. The script is chosen from the text language the
  user selected for recognition, else the UI language, else Latin.
- **Vietnamese.** The community Vietnamese PP-OCR recogniser enters the catalogue only after its weights licence is
  verified (§12). Until then Vietnamese text uses the system Tesseract with its `vie` language data, which AriadShot
  never downloads: no consent sheet is shown for it. When that data is not installed, Vietnamese recognition is
  `NeedsUserAction` with the reason "Install the Tesseract Vietnamese language data from your distribution" and the
  usual "How to enable" link ([08 §1](08-platform-integration.md#1-capability-registry)). [HYPOTHESIS: the relative
  accuracy of the two on UI screenshots is measured on a fixture set before either becomes the default.]
- **Fallback engine.** Tesseract 5 (system package) with the installed language data, used when the ONNX models are
  missing or for languages without a Paddle recogniser.
- **Boxes.** Lines keep their oriented quadrilateral and height, which the smart marker, the planner and the translate
  overlay need. Sub-string boxes come from the recogniser's per-character positions when the engine exposes them;
  otherwise `glyphBoxes` is empty and consumers fall back to the whole line (§8.3).

### 3.3 Consumers

| Consumer | Uses | MacShot source |
| :--- | :--- | :--- |
| Capture OCR & QR, OCR button (SH-19) | text of all lines joined with newlines, plus QR payloads; `ocrAction` 0 opens the window and copies (default), 1 window only, 2 copy only. The copied text is the recognised text, or the QR payloads joined by newlines when no text was found | `macshot/AppDelegate.swift:2642-2660`, `macshot/Services/VisionOCR.swift:17-25` |
| OCR result window (ED-10) | editable text, QR rows with Copy and Open Link (only `http`/`https` payloads open), translate header with the 30 languages, AI Search (opens a Google search for the text through the URL opener) | `macshot/UI/Windows/OCRResultController.swift:31-374` |
| Smart marker (AN-06b) | line bounds; the stroke snaps to y = line min + 0.55 × line height with width (H + 4) / 6; OCR runs once per selection and is cached | `macshot/UI/Tools/MarkerToolHandler.swift:132-270` |
| Censor "Text Only" (AN-09b) | every line box, padded 2 pt, one undo group | `macshot/UI/Tools/PixelateToolHandler.swift:60`, `macshot/Services/AutoRedactor.swift:117-160` |
| PII auto-redaction (AN-10) | lines and sub-string boxes (§8) | `macshot/Services/AutoRedactor.swift:274-323` |
| Translate overlay (AN-15, SH-20) | line boxes and text (§5.3) | `macshot/Services/TranslationOverlay.swift` |

Error texts are MacShot's: "No text found in selection.", "OCR failed: …" and "Translation failed: …"
(`macshot/Services/TranslationOverlay.swift:33-53`).

## 4. QR

- macOS: Vision barcode detection with symbologies QR and Micro QR only (`macshot/Services/VisionOCR.swift:62-84`).
- Linux: zxing-cpp restricted to `QRCode` and `MicroQRCode`, so AriadShot never reports a format MacShot would not.
- Both: payloads are trimmed, empty payloads dropped, duplicates in one image removed, order of first appearance kept.
  A payload is a link only when it parses as an `http` or `https` URL (`macshot/Services/VisionOCR.swift:3-15`).

## 5. Translation

### 5.1 Engines

| Engine | Platforms | Default | Notes |
| :--- | :--- | :-: | :--- |
| Google web endpoint | all | yes | MacShot's default provider; an unofficial endpoint with no service guarantee, used only on user action ([10](10-upload-and-network.md)). Kept in the offline build, as in MacShot |
| Apple Translation | macOS 15+ | no | MacShot's second provider; language availability probed and cached as MacShot does; the Linux settings section shows the Linux engines instead (ST-07, DEV-37) |
| Offline CTranslate2 with OPUS-MT or Argos models | Linux | no | per-language-pair models downloaded with consent (§13); permissively licensed weights only. NLLB-200 is excluded (CC BY-NC) |

The provider setting and the target language are MacShot's (`translationProvider`, default Google;
`translateTargetLang`, default `en`; `macshot/Services/TranslationService.swift:16-23`, `:37-40`). The target language list is
MacShot's 30 languages (`macshot/Services/TranslationService.swift:42-73`). The source language is detected
automatically, as MacShot's `sl=auto` and Apple language recognition do; the offline engine uses a text language
identifier to pick the source model and reports "language pair not installed" through the model flow when the pair is
missing.

### 5.2 Batch contract

`Translator::translate` takes the recognised lines of one selection as a batch and returns one translation per line
in order. Empty or whitespace-only lines pass through unchanged. If any line fails, the whole batch fails with the
first error, as MacShot's batch does (`macshot/Services/TranslationService.swift:163-201`); the caller shows MacShot's
"Translation failed" message.

### 5.3 Translate overlay

For each non-empty recognised line, in order (`macshot/Services/TranslationOverlay.swift:30-90`): the translated text
becomes a `TranslateOverlay` annotation covering the line box padded by 1 pt; its fill colour is the average colour of
the source pixels under the box (computed on a 4 × 4 downsample, in `render/pixel`); its font size is
max(8, 0.65 × padded box height), where the padded height is the line box height plus 1 pt on each side (`macshot/Services/TranslationOverlay.swift:65-68,85`); the text colour follows the contrast rule of AN-15; all overlay annotations share one group
ID. Lines whose translation is empty are skipped. "No text found in selection." is shown when OCR returns nothing.

## 6. Background removal

- macOS 14+: Vision foreground instance mask for all instances, applied to the image as alpha
  (`macshot/UI/Overlay/OverlayWindowController.swift:894-935`,
  `macshot/UI/Editor/DetachedEditorWindowController.swift:568-594`). Below macOS 14 the action is hidden, as MacShot
  hides it.
- Linux: BiRefNet-lite at 512 × 512 (MIT) downloaded on first use (§13); U²-Netp (about 4.7 MB) is **bundled** in the
  core model set (§12), so it is the fallback that works without any download when the user declines the large one.
  Its weights licence is recorded and verified in its catalogue entry like every other model's. Pre-processing (input
  size, resize filter, normalisation constants) is a per-model catalogue parameter taken from the model's published
  reference pipeline and verified on fixtures when the entry is added; for both models this is expected to be a
  bilinear resize with ImageNet mean and standard deviation [to verify per entry]. The single-channel mask is resized
  back to the source size and multiplied into the source alpha.
- The result is a new canonical image with transparency, then follows MacShot's route, which depends on the surface: in the overlay it is copied and/or saved according to the quick-capture mode, the capture sound plays, the overlay closes and the normal confirm path runs (`macshot/UI/Overlay/OverlayWindowController.swift:942-956`); in the editor it is copied to the clipboard and shown in the floating thumbnail (`macshot/UI/Editor/DetachedEditorWindowController.swift:585-590`). RMBG models are excluded (CC BY-NC).

## 7. Faces and people

- macOS: Vision face rectangles and human rectangles (`macshot/Services/AutoRedactor.swift:165-255`).
- Linux: YuNet for faces (Apache-2.0); a multi-person box detector, MobileNetV2-SSD or RT-DETR (Apache-2.0), for
  people. Single-subject pose models are not used, because MacShot redacts every person. Ultralytics YOLO models are
  excluded (AGPL).
- Both: every detection becomes one censor annotation, padded by 4 pt on each side, all in one group, using the censor
  tool, colour and censor mode the user has selected (`macshot/Services/AutoRedactor.swift:178-201`).
- Detection thresholds on Linux are fixed per model in the catalogue entry and measured against a fixture set; they
  are not user settings.

## 8. PII detection and auto-redaction (AN-10)

### 8.1 Patterns

The redaction categories and their patterns are ported from `macshot/Services/AutoRedactor.swift:10-46`: 11 categories
(`email`, `phone`, `ssn`, `credit_card`, `cvv`, `expiry`, `ipv4`, `aws_key`, `secret_assignment`, `hex_key`,
`bearer`) matched by 13 patterns (three of them for `credit_card`). The pattern strings are copied verbatim into one
constants header with their MacShot source line, and a test asserts each against the ledger value.

- Matching is case-insensitive, as MacShot compiles every pattern with the case-insensitive option.
- The engine is `QRegularExpression`. MacShot's patterns use no look-arounds or back-references, so they port without
  rewriting.
- Digit classes follow the architecture decision of ASCII digit semantics, so both platforms give identical results.
  MacShot's ICU `\d` and its planner's digit test also accept non-ASCII decimal digits; the difference is DEV-45.
- The enabled categories come from `enabledRedactTypes`; unset means all categories. MacShot's duplicate redaction
  flags collapse into this one key ([07](07-storage-history-settings.md)).
- No Luhn or entropy filter is added: MacShot's matcher has none, and adding one would change which text is covered.

### 8.2 Planner

`core::PiiRedactionPlanner` ports `macshot/Services/PIIRedactionPlanner.swift:1-163` behaviour for behaviour. Its input
is the recognised lines with bounds in image points; its output is (line index, category, text range) matches.

1. Lines with non-finite or empty bounds are ignored.
2. Every enabled pattern runs on every line. A match is dropped when a match of the same line and category already contains its range, and a new match removes every match of the same line and category whose range it contains; partially overlapping matches are both kept (`macshot/Services/PIIRedactionPlanner.swift:26-38`).
3. **Card joining.** Recognisers split one card number into several lines. Lines whose text is only numeric groups of
   3–6 digits (separated by spaces or hyphens) are grouped into rows (vertical overlap ≥ 0.6 of the smaller height,
   heights within a factor of 2), sorted left to right, and split into runs where the horizontal gap exceeds twice the
   larger height. Each run of two or more lines is joined with spaces and matched against the card patterns; a match
   counts only with at least three groups totalling 12–19 digits, and only when it spans at least two lines.
4. **Contextual CVV.** A line whose trimmed text is 3–4 digits, that is not part of a card, becomes a CVV match when it
   is near a label line reading `CVV`, `CVC`, `CSC` or `CCV` (with an optional colon) or near a detected card: same
   row within three line heights horizontally, or directly above or below within two line heights. Card context
   identifies a CVV even when only the CVV category is enabled, without selecting the card number itself.

### 8.3 Annotations

Each match becomes one censor annotation over the sub-string box of the matched range, padded by 2 pt, all in one
group; the censor tool, colour and censor mode are the user's current choices, and baked censor images are generated
as for hand-drawn censors (`macshot/Services/AutoRedactor.swift:274-323`). When the engine cannot give a sub-string
box, the whole line is covered, so detected text is never left exposed. Redact All Text covers every line box, padded
by 2 pt, in one group.

## 9. Captions (CR-22, VE-15)

- macOS: the Speech framework with on-device recognition required; recognition is refused when on-device support is
  unavailable for the language. Audio is recognised in 50 s chunks with punctuation, and word timings are offset by
  each chunk's start (`macshot/Capture/VideoCaptionTranscriber.swift:1-117`). The error messages are MacShot's.
- Linux: whisper.cpp with a model downloaded on first use (§13), run in chunks on the ML worker, producing the same
  word-timing structure. Nothing is uploaded; the studio's privacy note ("Nothing is uploaded") holds on both
  platforms.
- The caption language is the current system locale, as in MacShot. Captions are stored in the video project ([06](06-annotation-model-and-formats.md)) and exported as
  SRT with the project's time mapping ([05](05-recording-and-studio.md)).

## 10. Scroll registration

Scroll capture (CR-04) registers each new frame against the previous one. MacShot combines its own pixel logic
(scrollbar-width scan from the right edge, sticky-header detection, a minimum shift of one tenth of the frame height)
with Vision translational registration (`macshot/Capture/ScrollCaptureController.swift:445-786`,
`macshot/Capture/ScrollFrameAnalyzer.swift`). AriadShot ports the pixel logic unchanged and replaces the Vision call on
both platforms with phase correlation (vendored pocketfft) on downsampled frames, falling back to a SAD strip search
when the correlation peak is ambiguous. The implementation lives in `render/pixel/ScrollFrameAnalyzer`; the capture
flow is specified in [04](04-capture-and-overlay.md). No model is involved.

## 11. Effect presets

The Core Image photo-effect presets (SV-02) are reproduced on Linux by 3D LUTs fitted to renders from the MacShot
reference corpus (DEV-05); macOS uses Core Image directly. The LUT format and application are specified in
[03](03-rendering-contracts.md). No model is involved.

## 12. Model catalogue (Linux)

The catalogue is a JSON document compiled into the application (`resources/models/catalogue.json`); every model that
AriadShot can bundle, download or sideload has exactly one entry.

| Field | Meaning |
| :--- | :--- |
| `id` | stable identifier (`ocr-det-ppocr`, `ocr-rec-latin`, `bg-birefnet-lite-512`, …) |
| `version` | model version string; a new version is a new catalogue entry revision |
| `features` | capabilities that need it (`TextRecognition`, `BackgroundRemoval`, `Captions`, …) |
| `runtime` | `onnxruntime`, `ctranslate2`, `whispercpp` or `tesseract` |
| `files` | one or more files, each with `name`, `url` (HTTPS), `sha256` and `size` in bytes |
| `weightsLicence` | SPDX identifier of the trained-weights licence, verified by a maintainer |
| `source` | human-readable origin (project and page) shown in the consent sheet |
| `default` | whether the feature uses it without the user choosing it |
| `bundled` | whether packages ship it (only small core models) |
| `preprocess` | input size, resize filter and normalisation constants, verified against the model's reference pipeline |

- An entry whose weights licence is not verified is not in the catalogue. The community Vietnamese recogniser is such a
  case until its licence is confirmed (§3.2).
- Core bundle: the models a package ships preinstalled total at most 40 MB. This is the architecture record's budget,
  a target threshold kept or revised with the other budgets ([12 §9](12-testing-strategy.md)); larger models are
  downloaded on demand.
- Installed models live in `$XDG_DATA_HOME/ariadshot/models/<id>/<version>/`. Nothing else writes there.
- Every model file is verified against its SHA-256 before first use and after every download or import; a mismatch
  deletes the file and reports the failure (§13). Models are loaded only from this directory or the package's own
  read-only model directory.
- Every catalogue entry and its licence appear in the third-party notices ([13 §8](13-build-ci-release.md)).

## 13. Download flow (SH-33)

- **Consent.** The first use of a feature whose Linux model is missing (Remove Background, Vietnamese OCR, offline
  Translate, captions) opens a consent sheet in MacShot's dialog style showing the model name, what it enables, the
  download size, the weights licence and the source, with **Download** and **Cancel**. Nothing downloads without
  consent. Consent is per model and remembered.
- **Progress.** A progress ring appears on the invoking toolbar button or inspector row; a cancel control stays
  available. Cancelling deletes partial data. Downloads go to a temporary file in the models directory and are renamed
  into place only after the SHA-256 matches.
- **Failure.** A network error or a checksum mismatch shows an error pill in the OV-27 style, "Couldn't download
  <model>: <reason>", and leaves the feature disabled (`NeedsUserAction`) with a retry link.
- **Offline build.** There are no downloads (DEV-32). The features offer "Import model file…", which sideloads a
  user-supplied file and accepts it only when its SHA-256 matches the catalogue; otherwise the feature is disabled with
  that reason.
- **Management.** The Desktop Integration page lists catalogue models with status, size, licence, delete and
  re-download ([08 §11](08-platform-integration.md#11-desktop-integration-page-and-first-run-window)).
- **Network policy.** `ModelStore` never opens a connection: it asks the injected `ModelFetcher`
  (`net::ModelDownloader`) to stream the catalogue URL into its temporary file, and the downloader submits the request
  to `net::NetworkPolicy` with the purpose `modelDownload`. Downloads never run during a build or a test
  ([10 §9](10-upload-and-network.md)).
- **macOS** has no downloads; the Apple frameworks provide every model (DEV-20).

## 14. Quality and parity testing

- **Shared logic is tested exactly.** Planner, card joining, contextual CVV, translate-overlay layout, smart-marker
  snap and redaction geometry are tested with fixture line sets and assert MacShot's values; they run on every CI job.
  The PII pattern tests include MacShot's own pattern test cases (`macshotTests/AutoRedactorPatternTests.swift`,
  `macshotTests/PIIRedactionPlannerTests.swift`) ported one for one.
- **Recognisers are tested with tolerance.** Each Linux engine runs on a fixture set of UI screenshots (Latin, CJK,
  Vietnamese, QR, faces, people, subjects) with expected results and a minimum score per fixture (character accuracy
  for OCR, exact payloads for QR, IoU for boxes and masks). The thresholds are set when the engine lands and recorded
  in the test; a drop below them fails the test. These tests need the models and are labelled so that jobs without
  models skip them visibly, never silently pass.
- **Cross-platform differences** in recognised text or boxes are expected (DEV-06) and are not parity failures; parity
  lines for recognition features assert the downstream behaviour on fixed recogniser output.
- **Privacy.** A test asserts that no ML path opens a network connection except the consented download and the
  user-selected web translation.
