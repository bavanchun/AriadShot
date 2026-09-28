<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 07. Storage, History and Settings

Part of the [AriadShot technical specification](README.md). This file specifies where AriadShot keeps its data, the
settings registry and store, settings export and import, the history store, file saving and publishing, cleanup
sweepers and take storage. The classes named here live in `core/` ([02 §4](02-modules-and-interfaces.md)). Document
formats that these stores contain (annotations, edit state, video projects, telemetry, take status) are specified in
[06](06-annotation-model-and-formats.md); this file owns the containers around them: `settings.json`, the settings
export envelope, the history index and revision folders.

Parity rows covered: SH-23 (export and import), SH-24 (launch cleanup), SV-04 (saving), SV-05 (history), ST-02 to ST-05
(settings keys, recorders, filename preview, backup), and the storage side of SH-08, SH-34 and CR-08. Deviations used:
DEV-09, DEV-28, DEV-33.

## 1. Paths

Every path AriadShot writes is resolved by one class, `core::AppPaths` in `core/files/`, at start-up. Stores receive
their root directory from it by injection, never by calling `QStandardPaths` themselves. Tests therefore redirect every
store with `QStandardPaths::setTestModeEnabled(true)` and temporary `XDG_*` directories
([12](12-testing-strategy.md)), and a later instance namespace changes one class.

| Purpose | Linux | macOS | Created with |
| :--- | :--- | :--- | :--- |
| Settings document | `$XDG_CONFIG_HOME/ariadshot/settings.json` | `~/Library/Application Support/AriadShot/settings.json` | directory 0700, file 0600 |
| History | `$XDG_DATA_HOME/ariadshot/history/` | `~/Library/Application Support/AriadShot/history/` | 0700 |
| Recording takes | `$XDG_DATA_HOME/ariadshot/recordings/` | `~/Library/Application Support/AriadShot/Recordings/` | 0700 |
| On-device models (Linux only) | `$XDG_DATA_HOME/ariadshot/models/` ([09](09-ml-services.md)) | — | 0700 |
| Secret fallback file | `$XDG_DATA_HOME/ariadshot/secrets.json` ([11 §4](11-security-privacy.md#4-secrets)) | — (Keychain always present) | directory 0700, file 0600 |
| Scratch (share, drag-out, upload bodies, editor source copies, encode temporaries) | `$XDG_CACHE_HOME/ariadshot/tmp/` | `$TMPDIR/AriadShot/` | 0700 |
| Diagnostic logs | `$XDG_STATE_HOME/ariadshot/logs/` | `~/Library/Logs/AriadShot/` | 0700, files 0600 |
| Control socket | `$XDG_RUNTIME_DIR/ariadshot/` ([08 §5](08-platform-integration.md)) | per-user `$TMPDIR` ([08 §5](08-platform-integration.md)) | 0700 |
| Default save folder | `XDG_PICTURES_DIR` (`QStandardPaths::PicturesLocation`), else the home directory | `~/Pictures` | never created by AriadShot (§5.2) |

Rules:

- Unset `XDG_*` variables fall back to the XDG Base Directory defaults (`~/.config`, `~/.local/share`,
  `~/.cache`, `~/.local/state`), as `QStandardPaths` does.
- Scratch lives on disk, not in `$XDG_RUNTIME_DIR`: that directory is usually a RAM-backed tmpfs, and editor source
  copies of recordings can be several gigabytes. Scratch is never in the shared `/tmp`, so no other local user can
  pre-create or race a path in it.
- AriadShot never follows a symbolic link when it creates or deletes inside its own directories. The sweepers of §6
  skip symbolic links, as MacShot's directory sweeper does (`macshot/Services/VideoSourceSnapshot.swift:89-93`).
- The settings export, history and takes never contain the secret fallback file, and no store writes a path outside
  its root except the user's chosen save destination.

## 2. Settings registry and store

### 2.1 The typed registry

`core::SettingsRegistry` holds one descriptor per setting. It is the single source for effective defaults, UI
bindings, import validation and the portability class. Descriptor fields:

| Field | Meaning |
| :--- | :--- |
| `name` | the key; MacShot's key name wherever the value has the same meaning (§2.4) |
| `type` | `bool`, `int`, `double`, `string`, `stringList`, `color`, `accelerator`, `rect`, `blob`, or `json` (a nested document with its own schema, such as the studio look) |
| `effectiveDefault` | the value MacShot uses when its key is absent, from the getter or control that reads it |
| `domain` | the allowed values: an enumeration, a numeric range, a string pattern or a list shape |
| `parityDomain` | `shot`, `rec`, `studio` or `release` ([14](14-gates-and-milestones.md)) |
| `owner` | the owning surface: a Settings tab, the overlay or editor, the recording bar, the studio, or internal state |
| `offline` | whether the key exists in the offline build (MacShot's `!OFFLINE` keys) |
| `portability` | `portable`, `machine` (never exported) or `secret` (never stored here; kept in `platform::SecretStore`) |
| `macshotKeys` | the MacShot key or keys this descriptor maps (for the ledger and for a possible MacShot import later) |

The registry is **generated** from MacShot's key inventory. The inventory is extracted mechanically from MacShot source
by `tools/ledger/` (direct `forKey:` literals, constant-backed keys, and the finite families: twelve hotkey slots with
key code, modifiers and disabled flag, two editor command shortcuts, eleven redaction toggles). The result is reviewed
once against the source, then maintained through the ledger ([12](12-testing-strategy.md)). The key table is not
copied into this specification.

### 2.2 The settings document

`settings.json` is one JSON object, written atomically with `QSaveFile` (write to a temporary file in the same
directory, flush, rename), following the common format rules of [06](06-annotation-model-and-formats.md):

```json
{
  "format": "ariadshot-settings-store",
  "version": 1,
  "settings": {
    "quickCaptureMode": 1,
    "captureSnapMode": 2,
    "hotkey.captureArea": "Ctrl+Shift+4",
    "hotkey.history": null,
    "customColors": ["#FF3B30", "", "", "", "", "", ""]
  }
}
```

- **Unset is not default.** A key absent from `settings` is *unset*: its effective value is the registry default. A
  key present with the default value is *set*. `core::SettingsStore` keeps the two apart
  (`stored()` versus `effective()`, [02 §4](02-modules-and-interfaces.md)). "Reset to default" removes the key.
  Import depends on this distinction (§3), and the defaults-parity test checks both states against MacShot
  ([12](12-testing-strategy.md)).
- **Writes.** A change updates the in-memory store at once, and the store schedules one coalesced write on a worker
  thread. A failed write reports through MacShot's save-failure toast route (SH-02) and keeps the in-memory value.
- **Reading leniently.** A key whose value fails its descriptor's type or domain check reads as unset and is logged at
  debug level without its value. Unknown keys are preserved on rewrite, so a newer build's keys survive a session in an
  older build.
- **Corrupt document.** If `settings.json` does not parse, it is renamed to `settings.json.corrupt-<UTC timestamp>`,
  the session starts with defaults, and the error toast says the settings could not be read. Nothing is silently
  discarded.
- **Newer schema (downgrade).** A document whose `version` N is newer than the build understands is handled in one
  defined way:
  1. **Load.** It is read leniently: known keys with valid values are used, and every other key is kept verbatim in
     memory.
  2. **Preserve.** Before the session's first write, the store copies the untouched file byte for byte to
     `settings.json.v<N>.bak` (once; an existing backup of the same name is not overwritten). If the copy fails, the
     store writes nothing for the rest of the session: changes stay in memory and the save-failure toast (SH-02)
     reports that settings cannot be saved.
  3. **Write.** After a successful backup, a user change is saved normally, as a document of the build's own
     `version`, containing the known keys and every preserved unknown key verbatim.
  4. **Notice.** The first such write shows a one-time toast: "Settings were created by a newer version of AriadShot.
     Your changes are saved; the original file was kept as settings.json.v<N>.bak."

  A newer build that later reads the rewritten file therefore finds its own keys intact and can still recover the
  original from the backup.
- **Migrations** are functions from version *n* to *n + 1*, applied in memory at load, tested with fixture documents,
  and written back only when the user next changes a setting. MacShot's one-shot migrations (the effects migration,
  the filename checkbox migration) have no AriadShot counterpart because no AriadShot user holds that legacy state.

### 2.3 Capability coercion

Some stored values name a behaviour the current session cannot offer. Coercion happens when the effective value is
read, never by rewriting the stored value, so a settings document moved to another machine keeps its meaning:

- `captureSnapMode` (0 window, 1 element, 2 off) coerces an unavailable mode to the next available one in cycle order
  (element → window → off), as specified in [04](04-capture-and-overlay.md) (DEV-15, DEV-16).
- A hotkey slot or single-key action whose feature does not exist in the running build is not registered and not shown
  (M1 absent-feature rule, [14](14-gates-and-milestones.md)); its stored value is kept.
- Values outside a descriptor's range that MacShot's own reader clamps are clamped on read in the same way, citing the
  reader.

### 2.4 Key names and value encodings

AriadShot keeps MacShot's key names and value semantics wherever they are platform-neutral, so ledger lines, defaults
tests and a future MacShot import map one to one. The exceptions are fixed here:

| MacShot representation | AriadShot representation | Reason |
| :--- | :--- | :--- |
| Twelve hotkey slots as `hotkey…KeyCode` + `hotkey…Modifiers` integers (Carbon virtual key codes) plus `hotkeyDisabled_N` (`macshot/Services/HotkeyManager.swift:12-66, 205-233`) | one key per slot, `hotkey.<slot>`: absent = the platform's default table; `null` = cleared by the user (MacShot's `hotkeyDisabled_N`); otherwise an accelerator in `QKeySequence::PortableText` | Carbon key code 0 is both "unbound" and the `A` key; portable text is platform-neutral (Ctrl is ⌘ on macOS in Qt) and F-keys may be bare. Default tables per platform are in [08 §3](08-platform-integration.md) (DEV-13) |
| `editorCommandShortcuts.undo/redo` as JSON-encoded chord lists | the same keys, as lists of portable-text accelerators | platform-neutral chords |
| Archived `NSColor` data (theme wells) and hex strings (`customColors`) | `#RRGGBB` or `#RRGGBBAA` sRGB strings | no archived platform objects; colours are sRGB values ([02 §3](02-modules-and-interfaces.md)) |
| `NSRect` strings (`lastSelectionRect`, `lastSelectionScreenFrame`) | `rect` values `[x, y, width, height]` in global logical points, plus the output name | cross-platform geometry; the repeat-last-area rule (SH-21) compares the whole output frame |
| Security-scoped bookmarks (`saveDirectoryBookmark`, `recordingSaveDirectoryBookmark`) | not modelled; the path string alone | no sandbox bookmarks; the Linux build is not sandboxed and the macOS build keeps plain paths ([13 §7](13-build-ci-release.md#7-macos-distribution)) |
| Credentials in plain preferences (`s3AccessKeyID`, `s3SecretAccessKey`, `imgbbAPIKey`) and the Drive token file | `platform::SecretStore` entries; the settings document holds none of them | DEV-33 ([11 §4](11-security-privacy.md#4-secrets)) |
| The eleven per-category redaction booleans plus `enabledRedactTypes` | `enabledRedactTypes` only; the popover toggles read and write that list | the engine reads only `enabledRedactTypes` (`macshot/Services/AutoRedactor.swift:60, 95`); the booleans are duplicate popover state |
| `customColors` written but never reloaded (`macshot/UI/Overlay/OverlayView.swift:3127`) | written and reloaded | upstream defect fix, DEV-09 |
| Framework keys (`NS*`, `Apple*`, `SUHasLaunchedBefore`, `NSViewUsesAutomaticLayerBackingStores`) | not modelled; `SUEnableAutomaticChecks` and `betaUpdatesEnabled` keep their names for the update checks ([10 §8](10-upload-and-network.md)) | not AriadShot settings |
| Binary values (a custom beautify background image) | `blob`: stored inline as `{"__ariadshotData__": "<base64>"}` | the same tagged form the export uses (§3) |

Each exception is visible in the ledger through the descriptor's `macshotKeys` field, so the defaults-parity test still
compares against MacShot's effective default.

## 3. Settings export and import

AriadShot keeps MacShot's portability engine (`macshot/Services/SettingsPortability.swift:1-278`), with its own file
type. `core::SettingsPortability` implements it; Settings → General → Settings Backup exposes Export, Import and Reveal
settings file (ST-05, SH-23).

**Envelope.** MacShot's envelope fields are kept (`type`, `schemaVersion`, `appVersion`, `exportedAt`, `settings`;
`macshot/Services/SettingsPortability.swift:201-207`), so this is the one AriadShot document that does not use the
`format`/`version` pair of [06 §1](06-annotation-model-and-formats.md). Pretty-printed JSON with sorted keys:

```json
{
  "type": "ariadshot-settings",
  "schemaVersion": 1,
  "appVersion": "0.2.0",
  "exportedAt": "2026-01-31T09:30:00Z",
  "settings": { "quickCaptureMode": 1, "playCopySound": true }
}
```

The suggested filename is `ariadshot-settings-<yyyy-MM-dd>.json` (`macshot/Services/SettingsPortability.swift:36-40`).

**Which keys travel.** A key is portable only when every rule passes, in MacShot's order
(`macshot/Services/SettingsPortability.swift:117-129`):

1. It is not a machine key. MacShot's exclusion list (`macshot/Services/SettingsPortability.swift:50-67`) maps to the
   registry's `machine` class: save and recording folders, last selection geometry, pre-selection resolution preset
   state, camera and microphone device identifiers, migration bookkeeping (`knownToolRawValues`, `knownActionTags`),
   the Drive account e-mail (`gdriveUserEmail`) and the imgbb upload history with its delete URLs (`imgbbUploads`).
2. It does not look secret. The fail-closed substring guard is kept verbatim: any key containing, case-insensitively,
   `apikey`, `secret`, `token`, `password`, `credential`, `bookmark` or `s3` is never exported and never imported,
   even when a future key was not classified (`macshot/Services/SettingsPortability.swift:85-96`).
3. It is registered in the settings registry as `portable`. AriadShot's store holds no framework keys, so MacShot's
   shape rule and system-prefix denylist reduce to this registry check; the secret guard still runs first, so a
   mis-registered secret cannot pass.

**Binary values.** A `blob` larger than 2 MiB is skipped on export and reported to the user by name, never silently
dropped (`macshot/Services/SettingsPortability.swift:42-44, 146-149`).

**Import.** In MacShot's order (`macshot/Services/SettingsPortability.swift:244-276`):

1. Parse; a non-JSON file or a `type` other than `ariadshot-settings` shows "This file is not a valid AriadShot
   settings file."; a `schemaVersion` newer than the build shows the "made by a newer version" message; a missing
   `settings` object shows "This settings file contains no settings." (MacShot's strings with the product name
   replaced).
2. Filter every incoming key again with the portability rules, so a hand-edited file cannot inject a machine or secret
   key.
3. Validate every remaining value against its descriptor's type and domain; an invalid value is skipped and reported
   with the skipped keys. MacShot writes imported values without type checks; AriadShot validates because the file is
   untrusted input ([11 §2](11-security-privacy.md)). Valid files behave identically.
4. Decode everything before changing anything, so a bad file cannot half-apply.
5. **Replace portable:** remove every portable key currently set, then set the imported ones. A portable key absent
   from the file therefore returns to its effective default. Machine keys and secrets stay untouched.
6. Show the result with the applied count and skipped keys, then offer the relaunch prompt of the Settings Backup
   section (ST-05) so windows re-read their bindings.

An import file larger than 16 MiB is rejected as not valid before parsing [HYPOTHESIS: bound confirmed with the import
fuzz target]; valid exports are far smaller because each blob is capped at 2 MiB and portable scalars are small.

## 4. History store

`core::HistoryStore` ports MacShot's revision-directory history (`macshot/Services/ScreenshotHistory.swift`,
`macshot/Services/HistoryStorage.swift`), SV-05. Its guarantee: `kill -9` at any point loses no committed entry, and an
interrupted save never replaces a good revision with a partial one ([12](12-testing-strategy.md) robustness gates).

### 4.1 Layout

```text
history/
├── index.json                    the only commit point
└── <id>/                         one folder per history item (UUID, lowercase)
    └── <revision>/               one immutable folder per saved revision (UUID)
        ├── image.<fileExtension> final composited capture (PNG for every entry AriadShot creates)
        ├── thumb.png             cache: longest side ≤ 72 px (36 pt at 2×), never enlarged
        ├── preview.png           cache: longest side ≤ 480 px (240 pt at 2×), never enlarged
        ├── raw.png               present only for editable entries: the capture without annotations and effects
        ├── annotations.json      optional: annotation document ([06](06-annotation-model-and-formats.md))
        └── edit.json             optional: edit state ([06](06-annotation-model-and-formats.md))
```

The cache sizes follow `macshot/Services/HistoryStorage.swift:133-135` and the sizing rule of
`macshot/Services/HistoryImageSnapshot.swift:53-61` (scale = min(1, 2 × maximum points ÷ longest pixel side)). MacShot's
legacy flat layout (`<id>.<ext>`, `<id>_thumb.png`, …) is not supported: no AriadShot history predates revisions.

### 4.2 `index.json`

```json
{
  "format": "ariadshot-history-index",
  "version": 1,
  "entries": [
    {
      "id": "3f2a9c1e-5b7d-4e8a-9c0f-1a2b3c4d5e6f",
      "revision": "8d7c6b5a-4e3f-4a2b-9c1d-0e9f8a7b6c5d",
      "fileExtension": "png",
      "timestamp": "2026-01-31T09:30:00.125Z",
      "lastEditedAt": null,
      "pixelWidth": 2880,
      "pixelHeight": 1800,
      "hasAnnotations": true
    }
  ]
}
```

Fields and semantics are MacShot's `HistoryRecord` (`macshot/Services/HistoryStorage.swift:3-58`); dates are ISO 8601
UTC strings instead of Apple reference-date numbers (DEV-28). Row validation, applied per entry:

| Field | Rule | On failure |
| :--- | :--- | :--- |
| `id` | a UUID, 36 characters, compared case-insensitively | the row is rejected |
| `fileExtension` | one of `png jpg jpeg heic webp gif tiff tif` (lower-cased); missing means `png` | the row is rejected when present and not allowed |
| `timestamp` | a valid ISO 8601 date within the representable range | replaced by `1970-01-01T00:00:00Z` |
| `lastEditedAt` | same, or `null` | becomes `null` |
| `pixelWidth`, `pixelHeight` | integers | clamped to ≥ 0 |
| `hasAnnotations` | boolean, or absent | absent means not editable |
| `revision` | a UUID when present | the row is rejected: an invalid revision must never redirect to another file |

Unknown fields are preserved on rewrite. The entry order in the file is irrelevant: readers sort (§4.3).

### 4.3 Writing a revision

Every mutation of the index runs on one serial writer owned by `HistoryStore`. Saving a capture or an edit
(`macshot/Services/HistoryStorage.swift:118-171`):

1. Create `<id>/<revision>/` with mode 0700. It must not exist; a new revision always gets a fresh UUID.
2. Write `image.png`, `thumb.png`, `preview.png`, and for editable entries `raw.png`, `annotations.json` and `edit.json`.
3. `fsync` every file in the folder.
4. Compute the next index: replace any entry with the same `id`, order it, truncate it to the limit (§4.4).
5. Publish `index.json` through `AtomicPublish` in replace mode (§5.5). This rename is the commit.
6. After the UI adopts the committed list, delete exactly the folders the new index no longer references (obsolete
   revisions and pruned items, and the staged folder if the limit pruned it at once), then remove an `<id>/` folder that
   became empty. Deletion uses these exact paths, never a directory scan.

A failure before step 5 deletes the staged folder and reports "Could not save the screenshot to history." through the
save-failure toast (`macshot/Services/ScreenshotHistory.swift:196-198`); the previous index stays current.

A new capture gets `timestamp` = now and `lastEditedAt` = `null`; an edit saved from the editor keeps the entry's `id`
and `timestamp`, sets `lastEditedAt` = now and gets a new revision (`macshot/Services/ScreenshotHistory.swift:95-130`).

### 4.4 Limits, ordering and the save queue

- **Count limit.** `historySize` (default 10) or unlimited with `historyUnlimited`. A limit of 0 disables history: new
  captures are not added and the existing entries are cleared (`macshot/Services/ScreenshotHistory.swift:55-58, 97-98,
  215-218`). Changing the limit prunes.
- **Order.** Newest first by `lastEditedAt ?? timestamp` when `historyOrderByLastEdit` is on (default), otherwise by
  `timestamp`; ties keep their previous relative order (`macshot/Services/HistoryStorage.swift:173-179`). The limit
  truncates the ordered list, so the oldest entries go first.
- **Save queue.** At most 32 saves may be pending, and pending snapshot memory may not exceed 512 MiB, except that one
  save larger than the budget is admitted when no other save is pending (a long scroll capture). A save beyond either
  bound fails at once with "History is busy saving. Please try again shortly."
  (`macshot/Services/ScreenshotHistory.swift:63-68, 132-160`).
- **Removal.** Removing entries or clearing history hides them from every consumer immediately, then commits the
  shortened index through the same writer; the files go in step 6.
- **Quit.** The daemon waits for pending history writes before it exits, as MacShot's `waitUntilIdle` does
  (`macshot/Services/ScreenshotHistory.swift:191-194`).

### 4.5 Loading and salvage

At start-up (`macshot/Services/ScreenshotHistory.swift:63-90`):

1. Decode `index.json` strictly. If that fails, salvage it leniently: keep every row that validates and drop the rest.
2. Drop rows whose `image` file does not exist, and duplicate `id`s after the first.
3. Record whether the strict decode succeeded; only then may any automatic cleanup run (§4.6).
4. If the entry count exceeds the limit, prune.

### 4.6 Cleanup policy

Automatic cleanup never deletes a capture, a raw image, annotations or edit state. An index that is missing or decoded
only by salvage is not proof that anything is orphaned, so no cleanup runs at all in that case
(`macshot/Services/HistoryFileCleanup.swift:1-19`). In the revision layout the only automatic deletions are the exact
obsolete folders of step 6 of §4.3. A revision folder that no index references (a save interrupted between steps 1
and 5) is left on disk as a recovery candidate, as MacShot does; the user documentation explains how to recover an
image from it.

### 4.7 Opening an entry for editing

`edit?id=`, the history panel and Recent Captures open an entry in the editor under MacShot's rules
(`macshot/Services/ScreenshotHistory.swift:267-290`):

- The entry is editable only if `hasAnnotations` is true, `raw.png` loads, and `annotations.json` or `edit.json` (or
  both) exists.
- A present `annotations.json` must decode completely (strict mode of the annotation codec,
  [06](06-annotation-model-and-formats.md)); a present `edit.json` must decode, including its custom background image.
  If either fails, the editor opens the flat composited image instead. It never opens the raw image with an effect or
  annotation silently missing.
- An absent sidecar means "none": no annotations, or no edit state.

## 5. Saving and publishing

### 5.1 Save destinations

| Setting | Values | Effect |
| :--- | :--- | :--- |
| `saveAction` | 0 save to folder (default), 1 ask where to save | the Save route; Save As always asks (`macshot/Services/ImageSaveService.swift:3-29, 106-139`) |
| `quickCaptureMode` | 0 save, 1 copy (default), 2 save and copy, 3 nothing, 4 save and copy path | the Quick Save route; Confirm always copies (SH-17, [04](04-capture-and-overlay.md)) |
| `copyPathAfterSave` | default false | after a successful save, the saved file's path goes to the clipboard |

The image bytes come from `render/encode/ImageEncoder` with the chosen format and quality
([03](03-rendering-contracts.md)); saving never re-renders.

### 5.2 Save folder resolution

- `saveDirectory` unset means the default save folder of §1. `recordingSaveDirectory` unset means the same folder as
  screenshots.
- The configured folder itself is never created by a save. If it no longer exists or is not writable, Save to Folder
  asks for a folder through the file chooser (the FileChooser portal on Linux, `NSOpenPanel` on macOS), as MacShot asks
  when it cannot access its folder (`macshot/Services/ImageSaveService.swift:141-172`); cancelling that chooser is not
  an error.
- Subfolders produced by the filename template (§5.3) are created below the configured folder, never above it
  (`macshot/Services/ImageSaveService.swift:281-292`).

### 5.3 Filename templates

`core::FilenameFormatter` ports `macshot/Services/FilenameFormatter.swift:1-161`.

| Setting | Default |
| :--- | :--- |
| `filenameTemplate` | `Screenshot {date} at {time}` |
| `recordingFilenameTemplate` | `Recording {date} at {time}` |
| `filenameTemplateNoApp` | unset: use the main template even without an application name |

Tokens (case-sensitive): `{date}` = `yyyy-MM-dd`; `{time}` = `HH-mm-ss`; `{timestamp}` = `{date}_{time}`; `{unix}` =
epoch seconds; `{window}` = the captured window title, trimmed, or empty; `{app}` = the captured application's name, or
empty (AriadShot's own name counts as unknown); `{index}` = a sequence number or empty; `{random}` = eight characters
from `0-9a-z`, fresh per occurrence; `{yyyy}` `{MM}` `{dd}` `{HH}` `{mm}` `{ss}` = date parts; `{ms}` = milliseconds,
three digits. Dates use the POSIX locale and the local time zone.

Rendering rules:

1. An empty or whitespace-only template means the default template.
2. The template is expanded in one pass, left to right. Inserted titles and names are literal text, even when they
   contain something that looks like a token. Unknown tokens stay verbatim, so typos are visible.
3. If the template contains `{app}`, the application name is unknown and `filenameTemplateNoApp` is non-empty, that
   template is used instead.
4. Each `/` starts a subfolder. Each component is sanitised separately (§5.4); empty, `.` and `..` components are
   dropped, so the result always stays inside the save folder.
5. If the sanitised result is empty, the default template is rendered; if that is empty too, the name is `Untitled`.

The Settings filename field shows a live preview rendered with a fixed sample date and sample window and application
names (ST-04); the preview uses the same formatter, not a copy of it.

### 5.4 Sanitiser

`core::FilenameSanitizer` ports `macshot/Services/FilenameSanitizer.swift:5-31`, applied to every name component:
replace `/`, `:` and NUL with `-`; drop C0 controls, DEL and C1 controls while keeping format characters such as the
zero-width joiner in emoji; trim surrounding whitespace; keep whole characters up to 200 UTF-8 bytes; then strip
trailing dots and whitespace. The same rules apply on Linux and macOS, so a template produces the same name on both.

### 5.5 Atomic publishing

`core::AtomicPublish` ports `macshot/Services/AtomicMediaSave.swift:4-107` and the collision loop of
`macshot/Services/ImageSaveService.swift:350-391`:

1. Stage the complete output in a private directory on the destination's filesystem (a hidden
   `.ariadshot-staging-<uuid>/` next to the destination, removed afterwards), so the final rename never crosses
   filesystems.
2. Refuse to publish a staged file that is missing, not a regular file, or empty.
3. `fsync` the staged file (a deferred disk-full or I/O error surfaces here, while the destination is untouched).
4. Enter the publishing state (§5.6), then rename:
   - **No-overwrite mode** (every save into a folder, every recording export): `renameat2(…, RENAME_NOREPLACE)` on
     Linux, `renamex_np(…, RENAME_EXCL)` on macOS. On "exists", retry with `Name (2).ext`, `Name (3).ext`, … up to
     `Name (999).ext`, then with `Name <UUID>.ext`, until a rename succeeds. An existing file is never overwritten.
   - **Replace mode** (the history index, the settings document, a Save As target the user already confirmed in the
     file dialog): a plain atomic `rename`.
5. `fsync` the destination directory, so the new name survives a power loss.

On filesystems without `RENAME_NOREPLACE` (`EINVAL`), no-overwrite mode falls back to `link()` of the staged file to the
target name followed by `unlink()` of the staged name, which is also exclusive.

Copying a large source (a recording into an editor workspace) reads in cancellable 1 MiB chunks, uses a reflink
(`FICLONE`) where the filesystem supports it (APFS `clonefile` on macOS), and verifies afterwards that the source size
and modification time did not change during the copy (`macshot/Services/AtomicMediaSave.swift:19-76`).

### 5.6 Export jobs and cancellation

Every save, export and upload is a job registered with one export coordinator
(`macshot/Services/MediaExportCoordinator.swift:1-136`). Its cancellation token has four states — `running`,
`cancelled`, `publishing`, `finished` — under one lock:

- Cancel succeeds only in `running`.
- Publication begins only from `running`; after `cancelled` it fails with a cancellation error, so a cancelled job
  never publishes.
- Once `publishing`, Cancel is refused, so a file that was published is never reported as cancelled.

Quitting while jobs are active does not interrupt them: the daemon waits until every job finishes, then quits
(`macshot/Services/ApplicationTerminationCoordinator.swift:1-25`). The same drain covers pending history writes (§4.4).

### 5.7 Source leases for the studio

A video opened in the studio is read from a private working copy, so replacing the public file cannot change playback
or apply edits twice (`macshot/Services/VideoSourceSnapshot.swift:4-108`):

- A take owned by the take store (§7) is copied into an `Editor Sources/<uuid>/` folder beside the take, which is
  durable. Any other file is copied into `<scratch>/editor-sources/<uuid>/`, which is temporary.
- Each copy folder holds a `.readers` file locked with `flock(LOCK_EX | LOCK_NB)` while an editor or export reads it.
- A temporary copy is deleted when its last reader releases it. Saving back to the original path transfers ownership:
  the saved file is never deleted by the editor's close.
- The sweeper of §6 removes temporary copy folders older than 24 hours only when their `.readers` lock can be taken, so
  an editor that has been open for days keeps its copy.

### 5.8 Scratch files

Share-like flows that must hand another application a real file — drag-out from the thumbnail, history and pin, and
the Open With route — write to `<scratch>/share/<uuid>/<filename>`: one folder per item, so two items with the same
rendered name never overwrite each other while a receiving application reads them
(`macshot/Services/TmpScratchDirectory.swift:16-41`). Upload bodies are staged as `<scratch>/upload-<uuid>.tmp` with
mode 0600 ([10 §2](10-upload-and-network.md)).

## 6. Cleanup sweepers

`core::LaunchCleanup` runs once after launch on a background thread, like MacShot's launch cleaners
(`macshot/Services/LaunchCleanup.swift:129-150`). Each sweeper touches only AriadShot-owned names inside its own
directory, skips symbolic links, and uses a file's modification time for its age:

| Sweeper | Scope | Age | Deletes |
| :--- | :--- | :-: | :--- |
| Editor sources | `<scratch>/editor-sources/<uuid>/` | 24 h | folders whose `.readers` lock can be taken (§5.7) |
| Temporary files | `<scratch>/` top level | 24 h | AriadShot's own temporaries: upload bodies, microphone and encode scratch, UUID-named GIF and video intermediates, abandoned `Recording …` exports (`macshot/Services/LaunchCleanup.swift:178-245`) |
| Share scratch | `<scratch>/share/` | 5 min | every item folder (`macshot/Services/LaunchCleanup.swift:247-272`) |
| History | — | — | nothing (§4.6) |
| Takes | — | — | nothing (§7) |

MacShot's two legacy clipboard cleaners have no counterpart: AriadShot never had disk-backed clipboard files. The
sweepers never delete outside `<scratch>/`, never delete history, takes, models or settings, and log only counts.

## 7. Take storage

Recording takes are durable user data owned by `core::SessionStore` (CR-08). A take is a folder
`recordings/<uuid>/` containing the movie, the telemetry file, the optional camera track, the take status file
`session.json` and the studio project, whose formats are in [06](06-annotation-model-and-formats.md) and whose writer
states are in [05](05-recording-and-studio.md):

- The movie's display name is the sanitised recording filename (§5.4), `Recording` when empty; it is never a path
  (`macshot/Capture/RecordingSessionStore.swift:29-45`).
- A take folder is removed automatically only when recording start-up is cancelled before any media byte exists. Once
  bytes exist, the take is kept for recovery (`macshot/Capture/RecordingSessionStore.swift:55-60`).
- No sweeper, save, export failure or editor close deletes a take. The user deletes takes in the file manager.
- A path belongs to the take store if its resolved path lies inside the recordings root; that test decides whether a
  studio source copy is durable (§5.7).
- Show Recordings (SH-34, M3) reveals the recordings root through `platform::FileManager`, creating the folder if it
  does not exist yet (`macshot/AppDelegate.swift:2350-2358`).
- Exported recordings go to `recordingSaveDirectory` with the recording template through the no-overwrite publish of
  §5.5.

## 8. Consumers of the history store

`HistoryStore` publishes an ordered, read-only list of entries; the stores never hand out mutable records.

- **Recent Captures** (SH-08): the status menu lists entries as "W × H · time ago" with the thumbnail. The relative time
  follows MacShot's buckets: "just now" under 5 s, then seconds, minutes and hours, then the date as `MMM d, HH:mm`
  after 24 h (`macshot/Services/ScreenshotHistory.swift:18-31`); the strings come from the translation catalogue.
  Clicking copies the entry's image; Clear History asks for confirmation, then clears (§4.4).
- **History panel** (ED-07): the same list, loading `thumb.png` and `preview.png` on demand, falling back to the full
  image when a cache file is missing (`macshot/Services/ScreenshotHistory.swift:291-300`). Panel behaviour is in
  [04](04-capture-and-overlay.md).
- **`edit?id=`** and **Open in Editor** use §4.7. An unknown `id` shows the OV-27-style error pill.
- **Copy from history** re-reads `image.<ext>` and copies it with the same clipboard representations as a capture
  ([08](08-platform-integration.md)); history never stores clipboard formats.
