<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 11. Security and Privacy

Part of the [AriadShot technical specification](README.md). This file states what AriadShot handles, who it defends
against, and the rules that follow: bounds on untrusted input, parser hardening and fuzzing, secret storage, IPC and
URL hardening, the privacy contract of the optional Linux input helper, logging, privacy promises, build hardening,
supply chain and vulnerability reporting. Network behaviour is in [10](10-upload-and-network.md); storage paths and
permissions are in [07](07-storage-history-settings.md).

## 1. Threat model

**What AriadShot handles.** A screenshot tool sees whatever is on the screen: messages, documents, credentials shown in
terminals, banking pages. AriadShot handles:

| Asset | Where it lives | Why it matters |
| :--- | :--- | :--- |
| Captured pixels, recordings, microphone and system audio, camera track | memory while a capture or recording is active; history and takes on disk ([07](07-storage-history-settings.md)) | the most sensitive data the product touches |
| Clipboard contents, OCR text, typed annotation text, key telemetry | memory; annotation documents and take telemetry on disk | may contain secrets the user typed |
| Window titles and application names | memory; filenames when the template uses `{window}` or `{app}` | reveal what the user was doing |
| Upload credentials: S3 keys, a personal imgbb key, the Google Drive refresh token | `platform::SecretStore` (§4) | give access to the user's storage accounts |
| The user's files | save folders, history, takes | must never be overwritten or lost |
| Settings | `settings.json` | low sensitivity; may name private storage endpoints |

**Adversaries in scope.**

1. **Malicious content**: crafted images, videos, clipboard HTML or RTF, settings files, projects or history files that
   the user opens, pastes or imports. Goal: crash the resident daemon, execute code, or read files.
2. **Web pages and other applications** that invoke the `ariadshot://` URL scheme or hand AriadShot a file. Goal: make
   AriadShot capture, open or send something the user did not intend.
3. **Other local users** on the same machine. Goal: read captures, history or credentials, or plant files AriadShot
   will trust.
4. **Network attackers and hostile servers**: tampering in transit, or malicious responses from an upload,
   translation, update or model server.

**Out of scope, stated plainly.** Processes running as the same user can read every file AriadShot can, attach a
debugger and read the keyring while it is unlocked; nothing in a desktop application changes that. A compromised
compositor, kernel or firmware sees the screen before AriadShot does. Physical access is out of scope.

**Priorities**, in order: never leak captures or credentials off the machine without a user action; never lose or
overwrite user files; never crash the daemon on untrusted input; keep every degradation visible.

## 2. Untrusted inputs and their bounds

Every input below is parsed by bounded code that rejects out-of-range data before allocating for it. The bounds are
MacShot's where MacShot has one; AriadShot's additions are marked.

| Input | Entry points | Bounds and rules |
| :--- | :--- | :--- |
| Clipboard HTML (pin from clipboard, ED-09) | `ClipboardService` | ≤ 2 MiB input; ≤ 20,000 output characters; ≤ 20,000 nodes; depth ≤ 64; documents with entity declarations are rejected; external entities never load; a fixed tag allow-list with `font` and `a` rewritten to `span`; embedding, scripting and media tags (`script`, `style`, `iframe`, `object`, `embed`, `img`, `svg`, `video`, …) dropped with their content; any other tag unwrapped to its text; a fixed CSS property allow-list, each value ≤ 128 characters and validated; `colspan`, `rowspan` and `start` kept only as integers 1–100; a document that still references an attachment falls back to plain text (`macshot/Services/ClipboardHTML.swift:1-142`, `macshot/Services/ClipboardPinService.swift:20-79`) |
| Clipboard RTF (Linux: AriadShot's own reader) and RTFD (macOS) | `ClipboardService` | ≤ 2 MiB input; the rendered text is truncated at 20,000 characters on a character boundary with `\n…` appended (`macshot/Services/ClipboardTextPinRenderer.swift:7, 16, 48-70`) |
| Clipboard plain text | `ClipboardService` | 20,000 characters, as above; the rendered card is scaled so its area stays ≤ 24,000,000 pt² (`macshot/Services/ClipboardTextPinRenderer.swift:7, 140-146`) |
| Images: opened files, clipboard images, dragged-in files, custom stamps, custom backgrounds, portal screenshot files | `render/encode`, file routes | header inspected before decoding (`QImageReader::size()`); ≤ 128 Mi pixels and ≤ 128 MiB encoded (`macshot/Model/SavedCaptureValidation.swift:8-9`); Qt's image allocation limit set to the same pixel bound × 4 bytes; decoding only through the image plugins of the build's dependency set |
| Saved-capture sidecar images (`raw.png`, cached annotation PNGs) | history, annotation codec | PNG only, exactly one frame, fully decoded, within the pixel and byte bounds above (`macshot/Model/SavedCaptureValidation.swift:19-41`) |
| Annotation documents, edit state, video projects, telemetry, take status files | history, studio, recovery | lenient decoding bounded by MacShot's limits: coordinates finite with magnitude ≤ 1,000,000; rich-text payloads ≤ 4 MiB; per-field clamps; truncated telemetry accepted up to the last complete record ([06](06-annotation-model-and-formats.md); `macshot/Model/SavedCaptureValidation.swift:10-53`, `macshot/Model/AnnotationCodable.swift:250-253`) |
| History index | history store | per-row validation; invalid identifiers, extensions or revisions reject the row ([07 §4.2](07-storage-history-settings.md)) |
| Settings import file | Settings → Import | ≤ 16 MiB (AriadShot addition) [HYPOTHESIS: bound confirmed with the import fuzz target]; portability filter; per-key type and domain validation (AriadShot addition); decoded fully before anything changes ([07 §3](07-storage-history-settings.md)) |
| Videos opened in the studio | studio open route, takes | demuxed and decoded by FFmpeg libraries; AriadShot validates stream count, dimensions and duration it reports before building a project, and treats decoder errors as a failed open, never as a crash |
| URL-scheme URLs, CLI arguments, control-socket requests | `ControlServer`, URL handler | §5 |
| Network responses (uploads, sign-in, translation, update feed) | `net` | JSON and XML ≤ 4 MiB; XML parsed without DTD processing or external entities ([10 §10](10-upload-and-network.md)) |
| Model files | `ml::ModelStore` | loaded only when the SHA-256 matches the catalogue entry, whether downloaded or sideloaded ([09](09-ml-services.md)) |
| Portal and D-Bus replies | `backends/portal` | typed through QtDBus; a returned file URI must be a local regular file; the Screenshot portal's file is read as an image under the image bounds and then deleted |
| Input helper events | `InputTelemetry` | fixed-size records, validated per field (§6) |

Size arithmetic (width × height × bytes per pixel, buffer strides, offsets) uses checked integer helpers in `core`,
as MacShot's `SafeNumerics` does (`macshot/Capture/SafeNumerics.swift`); an overflow is a rejected input, never a
wrapped allocation.

## 3. Parser hardening and fuzzing

AriadShot is C++, so the compensation for memory safety is written into the acceptance criteria
([ADR 0001](../adr/0001-architecture-and-stack.md), [12](12-testing-strategy.md)):

- **Shape of a parser.** Every parser of untrusted input is a function from bytes and explicit limits to
  `ariadshot::Expected<T, ParseError>`, with no exceptions and no global state. Parsers live in `core` (documents,
  settings, history, telemetry, clipboard text) or in `render/pixel` (anything that reads pixels). Pixel loops exist
  only in `render/pixel`, behind bounds-checked `std::span` accessors ([02 §5](02-modules-and-interfaces.md)).
- **One fuzz target per parser**, added in the pull request that adds the parser, before it merges (`tests/fuzz/`,
  libFuzzer with ASan and UBSan). Initial targets: the settings document and import envelope, the history index, the
  annotation document including its rich-text payload, the edit state, the video project, the telemetry reader, the take
  status file, the clipboard HTML sanitiser, the Linux RTF reader, the control-socket request parser, the URL-scheme
  parser, the update feed parser and each network response parser.
- **Corpora** start from valid documents written by AriadShot and from the malformed cases of MacShot's robustness and
  legacy-decoding tests (`macshotTests/RobustnessFuzzTests.swift`, `macshotTests/LegacyDecodingTests.swift`). Every
  crash found becomes a committed regression input.
- **Runs.** Every target passes a 24-hour run before each milestone release (robustness gate,
  [12 §11](12-testing-strategy.md)); every committed regression input runs as an ordinary test in the `asan` suite.
- **Escape hatch.** If fuzzing in M1–M2 finds memory-safety defects in AriadShot's parsers faster than they are fixed,
  the untrusted-input parsers move into a small Rust static library with a C ABI; this is the first response, before any
  stack change ([ADR 0001](../adr/0001-architecture-and-stack.md)).

## 4. Secrets

**What is secret.** Exactly four kinds of value, all upload credentials:

| Secret | Store key |
| :--- | :--- |
| S3 access key ID | `s3.accessKeyId` |
| S3 secret access key | `s3.secretAccessKey` |
| A personal imgbb API key | `imgbb.apiKey` |
| The Google Drive refresh token | `gdrive.refreshToken` |

MacShot keeps the first three in plain preferences and the Drive token in a 0600 file. AriadShot moves all four behind
`platform::SecretStore` and never writes them to `settings.json`, the settings export, history, logs, IPC replies or
command output (DEV-33). The Drive access token exists only in memory.

**Stores.** `net::KeychainSecretStore` uses QtKeychain: the Secret Service (`org.freedesktop.secrets`, provided by
GNOME Keyring, KeePassXC and similar), KWallet on KDE, and the Keychain on macOS. The service name is `AriadShot`, the
account is the store key. At start-up the store is probed once; a backend that does not answer within 2 s counts as
unavailable for this session [HYPOTHESIS: timeout set with the first implementation].

**Fallback file** (Linux only, when no keyring answers): `net::FileSecretStore` at
`$XDG_DATA_HOME/ariadshot/secrets.json`, a JSON map from store key to value, with the `format`/`version` pair of
[06 §1](06-annotation-model-and-formats.md):

- the directory is 0700 and the file 0600, created with those modes and published atomically (the mode is set on the
  staged file before the rename, so no window exists in which the file is readable by others);
- before every read AriadShot checks that the file and its directory are owned by the user, are not symbolic links and
  grant nothing to group or others; otherwise it refuses to read, shows the error and asks the user to fix the
  permissions, as OpenSSH does for private keys;
- the file is excluded from the settings export and never lives inside history or takes.

**Threat model of the fallback.** It protects against other local users, against accidental inclusion in settings
exports and backups of the settings file, and against casual reading. It does **not** protect against other processes
running as the same user. A key derived from the machine ID or the user ID would be obfuscation, not encryption,
because any process of the same user can derive it too; none is used.

**Visible degraded state.** While secrets live in the fallback file, the Uploads tab shows "Credentials are stored in a
file readable by your user account (no keyring found)" with a link to the Desktop Integration page
([08](08-platform-integration.md)).

**Moving between stores is never silent.**

- When a keyring becomes available while secrets are in the file, the Uploads tab offers **Move to keyring**. The move
  writes each secret to the keyring, reads it back, and only then deletes the file.
- When secrets are in the keyring and the keyring does not answer, the providers show "Credentials unavailable
  (keyring not responding)". AriadShot never falls back to writing them to the file on its own.
- Entering a credential in the Uploads tab writes it to the store that is active; secure fields are masked and do not
  allow copying the value out.

**macOS.** The Keychain is always used. MacShot kept the Drive token in a file to avoid Keychain access prompts. Because
AriadShot's macOS builds are ad-hoc signed ([13 §7](13-build-ci-release.md#7-macos-distribution)), a Keychain item's
access list may stop matching after each update and prompt again. Gate G7's signing experiment records whether items
stay readable across updates, and the result is decided together with the signing identity question
([14 §2](14-gates-and-milestones.md)) [GATE G7].

## 5. IPC, URL scheme and file routes

**Control socket** (protocol and path: [08 §5](08-platform-integration.md)):

- The runtime directory is created by the daemon with mode 0700 and must be owned by the user and not be a symbolic
  link; otherwise the daemon refuses to listen and reports why. The socket file is 0600.
- Every connection's peer credentials are checked (`SO_PEERCRED` on Linux, `getpeereid` on macOS); a peer with a
  different user ID is disconnected without a reply.
- One request per line, at most 64 KiB, parsed by the bounded request parser (§3). Unknown actions and malformed
  parameters are rejected with an error reply; nothing is executed partially.
- Replies carry only the fields of the reply schema ([08 §5.2](08-platform-integration.md#52-control-protocol)): a status, a code and a message. They never carry pixels,
  clipboard contents, recognised text or secrets.
- A stale socket from a crashed daemon is replaced only after a connection attempt fails and the single-instance lock
  in the same directory can be taken.

**URL scheme** (`ariadshot://`, SH-04):

- All URL actions are disabled when `urlSchemeEnabled` is off (default on, `macshot/AppDelegate.swift:2402-2403`).
- The host is the action; only MacShot's action names are accepted. Unknown actions are ignored and logged at debug
  level; MacShot actions this build does not offer show the absent-feature error pill ([14](14-gates-and-milestones.md)).
- Parameters are validated before use: `open?file=` must be an absolute local path to an existing regular file with
  an extension the file router accepts (`macshot/AppDelegate.swift:2396-2425`, action at `:2457-2461`): images open in the
  editor, and `mp4`, `mov` and `m4v` open in the studio once the studio exists (DEV-46); `edit?id=` must be a UUID of
  an existing history entry; `ocr-translate?target=` is trimmed, and a missing or empty value uses the saved
  target, as in MacShot (`macshot/AppDelegate.swift:2444-2449`); any other value goes to the translator, whose error
  surfaces as usual.
- No URL action uploads, deletes, writes to a path taken from the URL, or changes a setting. Capture-class actions
  start the normal capture flow, subject to the request queues ([08 §4](08-platform-integration.md)). Browsers ask the
  user before they hand a custom scheme to an application.

**File routes** (Open With, drag-in, command line, Open Image…, Open Video…): only local regular files are opened; a
remote or non-file URL is refused. Files are read as untrusted input under §2 and are never executed or interpreted as
anything but images and videos.

**No shell interpolation.** External programs run through `QProcess` with argument lists, never a shell string. The
Hyprland managed bindings file ([08 §3](08-platform-integration.md)) is generated only from accelerators that parsed
under the accelerator grammar and from AriadShot's fixed action names; no user-supplied text reaches that file
unvalidated.

## 6. Input helper privacy contract

On Wayland no protocol offers passive click and key events (DEV-24). Clicks and keystrokes for recordings — the live
click ring (CR-17), the keystroke pill (CR-18), click and key telemetry for the studio (CR-15) and auto-zoom (VE-07) —
therefore need an optional Linux input helper. Its process and privilege model (for example a udev rule or `input`
group membership) is decided by an ADR when it is built in M3. Whatever that ADR chooses, it must satisfy this
contract:

1. **Opt-in only.** AriadShot never installs, enables or requests the helper by itself. The Desktop Integration page
   explains what it does and what access it needs, and the user installs it knowingly. Without it, the dependent
   options are shown disabled with a reason and everything else works.
2. **Active only while needed.** The helper delivers events only while a recording is running whose options need them:
   an editable-mode recording with Keystrokes or click telemetry enabled, or a recording showing the live click ring or
   keystroke pill. MacShot records key telemetry only when the Keystrokes option is on in an editable-mode recording
   (`macshot/AppDelegate.swift:2948-2970`). The daemon starts and stops the event stream; the helper keeps no history.
3. **Local, user-only channel.** Events go only to the daemon of the same user, over a local channel in the user's 0700
   runtime directory, with peer credentials checked in both directions. The helper has no network code and links no
   network library.
4. **Minimal events.** Key down and up with the key code, modifiers and the text the active keymap produces; pointer
   button down and up; monotonic timestamps. No device identifiers beyond what the channel needs; pointer position
   comes from the compositor-side cursor telemetry, not from the helper.
5. **Persistence only in the take.** Events are written only into the take's telemetry file (mode 0600,
   [06](06-annotation-model-and-formats.md)) under MacShot's rules, and only for the recording that asked for them. They
   never reach logs, settings, history, crash logs or the network. The live pill displays events and forgets them.
6. **Filtering as MacShot.** The live pill shows shortcuts only unless "Show all keystrokes" (`keystrokeShowAll`,
   default off) is on (`macshot/UI/Overlay/KeystrokeOverlay.swift:110-127`). Recorded telemetry keeps typed text as
   MacShot does, because the studio's keystroke look can switch between shortcuts only and all keystrokes after
   recording (`macshot/Capture/KeystrokeTimeline.swift:28-60`).
7. **Password caveat, disclosed.** On macOS, secure text fields keep keystrokes away from event taps, so MacShot never
   records a password typed into one. A Linux input helper cannot tell a password field from any other field. The
   Keystrokes option on Linux therefore states, when the user first enables it, that everything typed during the
   recording is recorded, including passwords.
8. **Visible while active.** The helper is only active during a recording, whose indicators (tray stop icon, HUD, the
   recording outline) are visible; where the HUD is hidden (DEV-22) the tray and the Record hotkey remain.
9. **Removable.** The Desktop Integration page shows how to remove the helper and its access; AriadShot keeps working
   without it.

## 7. Logging and diagnostics

- **Categories.** One `Q_LOGGING_CATEGORY` per module (`ariadshot.<module>`). Release builds log warnings and errors;
  debug output is enabled per category with `QT_LOGGING_RULES`.
- **Never logged**, at any level: pixel data, clipboard contents, recognised or translated text, typed text and key
  events, secrets, tokens, API keys, authorization codes, request URLs with query strings, request or response bodies,
  and file contents. Window titles and application names are not logged either, because filenames and snapping use
  them. File paths are logged only at debug level, because they contain user names.
- **Destinations.** Logs go to standard error (the user session's journal on Linux, the unified log on macOS). AriadShot
  writes no log files except the crash and termination records below.
- **Crash and termination records** (SH-29, DEV-47). On macOS AriadShot keeps MacShot's diagnostic: a one-line record written
  with async-signal-safe calls when SIGTERM arrives, to tell memory-pressure kills from normal termination, in
  `~/Library/Logs/AriadShot/termination.log` (`macshot/AppDelegate.swift:89-112, 657-664`). On Linux SIGTERM is a normal
  clean quit through the signal bridge; for fatal signals an async-signal-safe handler appends one line (signal,
  version, UTC time) and raw return addresses to `$XDG_STATE_HOME/ariadshot/logs/crash.log` (0600), then re-raises.
  No memory dump, register contents or user data is recorded.
- **No automatic reporting.** Nothing is sent anywhere. A user may attach a record to a bug report; the bug template
  asks them to read it first.

## 8. Privacy promises

These are product promises; a change to any of them is an ADR and a release-note item.

1. **Zero telemetry.** No analytics, usage statistics, crash upload or identifiers, in any build.
2. **Network only on the user's behalf**: uploads, sign-in, translation, update checks and consented model downloads
   ([10 §1](10-upload-and-network.md)). The offline build removes uploads and sign-in ([10 §6](10-upload-and-network.md)).
3. **On-device recognition.** OCR, QR decoding, face and person detection, subject masks, PII detection and captions
   run on the device ([09](09-ml-services.md)). MacShot's captions note, "Captions are transcribed on this Mac. Nothing
   is uploaded." (`macshot/UI/Editor/Video/VideoInspectorView.swift:605`), stays true on both platforms: macOS requires
   on-device speech recognition as MacShot does (`macshot/Capture/VideoCaptionTranscriber.swift:30, 84`), and Linux uses
   a local whisper.cpp model, with "Mac" replaced by a platform-neutral word in the Linux string. The web translation
   engine is the one exception, named as such in the engine picker ([10 §7](10-upload-and-network.md)).
4. **No retained pixels.** The idle daemon holds no screenshot pixels: dismissing the overlay releases every image and
   keeps only the surfaces (OV-29). Clipboard data served through data-control is the content the user copied, for as
   long as it is the current selection.
5. **Recording consent.** Microphone, system audio, camera and input telemetry are captured only when enabled for that
   recording, and a recording always has visible indicators. Where Wayland cannot exclude AriadShot's own surfaces from
   a recording, the limitation is disclosed (DEV-22, DEV-23).
6. **Local data stays local and under the user's control.** History is bounded by the user's limit; takes stay until the
   user deletes them; everything lives in the user's directories with private permissions ([07 §1](07-storage-history-settings.md)).
7. **Clipboard content never fetches.** Pinned clipboard HTML never loads remote or local resources (§2).

## 9. Build hardening and sanitizers

- **Release builds on Linux** use `_FORTIFY_SOURCE=3`, `_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
  `-fstack-clash-protection`, `-fcf-protection` on x86-64, position-independent executables and `-Wl,-z,relro,-z,now`.
- **macOS**: libc++ hardening mode and the hardened runtime are configured and verified with signing; whether the
  hardened runtime is enabled for ad-hoc-signed releases is decided at G7 by measurement ([13 §7](13-build-ci-release.md#7-macos-distribution)).
- **Sanitizers.** The `asan` preset runs the whole offscreen test suite under AddressSanitizer and
  UndefinedBehaviorSanitizer; the `tsan` preset runs the tests labelled free of Qt threading under ThreadSanitizer.
  Leak suppressions name single allocation functions with a reproduced stack, never whole libraries. Fuzz targets build
  with ASan and UBSan ([13](13-build-ci-release.md)).
- **Robustness gate.** No crash in 24 hours of scripted capture and annotate cycles under ASan and UBSan before each
  milestone release ([12](12-testing-strategy.md)).
- **Qt definitions** that remove unsafe implicit conversions are on for every target (`QT_NO_CAST_FROM_ASCII`,
  `QT_NO_CAST_TO_ASCII`, `QT_NO_URL_CAST_FROM_STRING`, `QT_NO_NARROWING_CONVERSIONS_IN_CONNECT`).

## 10. Supply chain

- **No downloads while building.** Configure, build and test fetch nothing; `FetchContent`, `ExternalProject` and
  `file(DOWNLOAD)` are banned and a policy check enforces it in CI ([13](13-build-ci-release.md)). Dependencies come
  from distribution packages or are vendored under `third_party/` with their licence and origin.
- **Pinned automation.** CI actions are pinned by commit SHA, the CI Qt version is pinned, Dependabot updates only the
  action pins, and pull-request jobs receive no secrets. Build caches are treated as untrusted input and hold no
  credentials; release jobs build without restored caches.
- **Secret scanning.** gitleaks runs in the pre-commit and pre-push hooks and over every pull request's range in CI;
  GitHub secret scanning and push protection are enabled on the repository.
- **Models.** Every downloadable model is pinned by SHA-256 and carries a verified weights licence in the catalogue
  ([09](09-ml-services.md)).
- **Updates.** macOS updates are accepted only with a valid EdDSA signature from the project key; Linux relies on the
  package manager's own verification ([13 §7](13-build-ci-release.md#7-macos-distribution)).

## 11. Vulnerability reporting

- `SECURITY.md` routes reports to GitHub Private Vulnerability Reporting on the AriadShot repository, defines the scope
  (the application, its packaging and update feed; not MacShot, Qt, FFmpeg or other upstreams, whose own channels are
  linked) and promises a best-effort response without a fixed deadline.
- Fixes ship as patch releases (`v0.N.P`) with a GitHub security advisory; the release notes name the affected versions.
- **Update-key custody.** The Sparkle EdDSA private key is generated and kept by the maintainer on the maintainer's Mac,
  with an offline backup; it never exists in CI or in any automated agent environment. Key rotation needs Apple code
  signing, which ad-hoc builds lack, so a lost or leaked key cannot be rotated: users must reinstall manually from the
  release page, and the security policy says so ([13 §7](13-build-ci-release.md#7-macos-distribution)).
