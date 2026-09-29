<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 08. Platform Integration

Part of the [AriadShot technical specification](README.md). This file specifies how AriadShot learns what the
current desktop can do, and how it integrates with it: hotkeys, the tray or status item, the clipboard, single
instance and IPC, the command-line client, the URL scheme and file routes, desktop portals, the macOS shims, the Linux
substitutes for macOS-only services, the Desktop Integration page, launch at login, sounds, keyboard layouts, system
events and runtime language switching. Module ownership follows [02](02-modules-and-interfaces.md); the decisions are
in [ADR 0001](../adr/0001-architecture-and-stack.md).

## 1. Capability registry

`app::CapabilityRegistry` is the single run-time record of what this session can do. It implements the capability
vocabulary declared in `platform` (`platform::Capability`, `platform::CapabilityState`, `platform::CapabilityStatus`)
and the abstract query interface `platform::Capabilities`, which `ui`, `windows` and `app` consume. UI code asks the
registry; it never checks the desktop, compositor or distribution name (invariant 1 of
[01 §4](01-architecture-overview.md)).

### 1.1 Vocabulary

Every capability has one of four states, a one-line user-facing reason, and a fix route:

| State | Meaning | How the UI shows it |
| :--- | :--- | :--- |
| `Available` | works in this session | normally |
| `Degraded` | works, with a documented limitation (portal dialog, no on-top guarantee, focused-only clipboard) | normally, plus a dismissible helper badge on the first occurrence and a line on the Desktop Integration page |
| `NeedsUserAction` | could work after a user action (grant a portal permission, install the input helper or the GNOME tray extension, download a model, install the desktop file) | the control is shown **disabled** with the one-line reason and a "How to enable" link to the Desktop Integration page |
| `Unavailable` | can never work in this session (element snapping on Linux, Share on Linux) | the control is **hidden**, as MacShot hides actions its system cannot perform, for example Remove Background before macOS 14 |

```cpp
namespace ariadshot::platform {

enum class Capability {
    FreezeCapture, WindowCapture, OverlayAbovePanels, WindowSnapping, ElementSnapping,
    GlobalShortcuts, ShortcutReadBack, Tray, BackgroundClipboard, FloatingPanels, AutoScroll,
    ScreenRecording, SystemAudio, Microphone, Camera, CursorTelemetry, InputTelemetry, SelfExclusion,
    ShareSheet, QuickLook, OpenWithChooser, RevealInFileManager, Wallpaper, FocusReturn, LaunchAtLogin,
    TextRecognition, BackgroundRemoval, FaceAndPeopleDetection, OfflineTranslation, SystemTranslation, Captions,
};

enum class CapabilityState { Available, Degraded, NeedsUserAction, Unavailable };

struct CapabilityStatus {
    CapabilityState state;
    QString reason;          // translated, one line, empty when Available
    QString fixAnchor;       // anchor on the Desktop Integration page, empty when there is no fix
};

class Capabilities {         // implemented by app::CapabilityRegistry; Thread: GUI
public:
    virtual CapabilityStatus status(Capability) const = 0;
    virtual CaptureAvailability captureAvailability() const = 0;   // §4
    // Signal equivalent: the registry emits changed(Capability) through its QObject implementation.
};

} // namespace ariadshot::platform
```

The mapping from a capability state to its deviation-register entry lives in the deviation register, not in code.
Every `Degraded`, `NeedsUserAction` and `Unavailable` outcome for a MacShot behaviour must name a `DEV-nn` entry
([14 §5](14-gates-and-milestones.md#5-initial-deviation-register)) before the ledger lines it affects count on that
platform, so parity is always reported with its exceptions. The matrix of §2 names the entry in each such cell; a cell
marked "to register" has none yet, and its entry is added to the register with the backend that produces it (KDE and
generic wlroots at M2 for the M1 atomic set, full tier promises at M5).

### 1.2 Resolution

- The registry resolves every capability at start-up and again on output hotplug or geometry change, when a relevant
  D-Bus name appears or disappears (portal frontend, `org.kde.StatusNotifierWatcher`, `org.freedesktop.FileManager1`,
  `org.freedesktop.secrets`), after a user action that changes it (model downloaded, helper installed, keyring
  unlocked), and on wake from sleep.
- Evidence comes only from probes: Wayland globals advertised on AriadShot's own connection (with their versions),
  portal interfaces and their `version` properties, D-Bus name owners, IPC endpoints that answer (the Hyprland and
  Sway IPC sockets), X11 extensions, macOS API availability and permission preflight calls, installed model files.
  Advertised is not usable: a capability that a gate has not yet exercised on a platform stays at most `Degraded`
  in the parity report until the gate passes (G2 for capture, G4 for background clipboard, G6 for recording).
- Probing runs off the GUI thread where it may block (D-Bus calls, socket connects) and is bounded: the capture
  capability must resolve within 2 s of start-up ([§4](#4-request-queues-and-capture-availability)); anything still
  unresolved after its bound is `Unavailable` with the reason "didn't respond" and is retried on the next trigger.
- Desktop-name checks (`XDG_CURRENT_DESKTOP`, compositor names) are forbidden outside `backends/`, and backends use
  them only to choose which probes to run, never to decide an outcome. `scripts/check-architecture.sh` enforces the
  first half.

### 1.3 Communication rules

1. **Never available** in this session: hide the control, its menu item, its toolbar button, its settings row and its
   single-key shortcut row.
2. **Available after a user action**: show the control disabled with the reason and a "How to enable" link.
3. **Degraded but working**: on the first occurrence per session show a dismissible helper badge drawn in the
   overlay's helper-card style (OV-04), and list the limitation on the Desktop Integration page ([§11](#11-desktop-integration-page-and-first-run-window)).
4. Hidden and disabled controls keep their settings values; a settings file moved to another platform keeps them.
5. A capability that changes state while a surface shows it updates the control in place; it never closes the
   surface.

## 2. Capability matrix

The expected resolution per desktop. The registry decides at run time; this table is what the probes are expected to
find and what CI and the gates measure ([14 §3](14-gates-and-milestones.md#3-platform-feature-gates)). Cells name the
mechanism, then the state when it is not `Available`.

| Capability | Hyprland (reference) | Other wlroots (Sway) | KDE Plasma 6 | X11 | GNOME Wayland | macOS |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Freeze-frame capture (SH-16, CR-01, CR-02) | `ext-image-copy-capture-v1`, `wlr-screencopy` fallback, no prompt [GATE G2] | image-copy or screencopy | image-copy if KWin allows it, else ScreenCast with a restore token (`Degraded` on first consent; to register) | XShm | Screenshot portal; may show a dialog; whole desktop split by output (`Degraded`, DEV-26) | ScreenCaptureKit (Screen Recording permission) |
| Overlay above panels on every output (OV-01) | layer-shell surface [GATE G1] | layer-shell | layer-shell | override-redirect | fullscreen toplevel per output; panel coverage and animation not guaranteed (`Degraded`, DEV-26) | `NSPanel` level 257 |
| Window snapping (OV-08) | Hyprland IPC window list | Sway IPC; other compositors `Unavailable` (to register) | `Unavailable`, boundary snapping only (DEV-17) | EWMH | `Unavailable`, boundary snapping only (DEV-17) | CGWindowList |
| Element snapping (OV-09) | `Unavailable` (DEV-16) | `Unavailable` (DEV-16) | `Unavailable` (DEV-16) | `Unavailable` (DEV-16) | `Unavailable` (DEV-16) | Accessibility API (`NeedsUserAction` until granted) |
| Window capture with real alpha (CR-02, beautify) | `hyprland_toplevel_export_v1` with the IPC window address | foreign-toplevel capture source where advertised | `Unavailable`: no window snapping, so no window-snap beautify (DEV-17) | XComposite | `Unavailable`: no window snapping, so no window-snap beautify (DEV-17) | ScreenCaptureKit |
| Global hotkeys (SH-10) | GlobalShortcuts portal, `hyprland_global_shortcuts_v1` fallback; keys set in Hyprland config (DEV-14) | portal where present, else compositor binds calling the CLI | GlobalShortcuts portal | `XGrabKey` | GlobalShortcuts portal (GNOME 48+) | Carbon |
| Shortcut read-back (SH-09, ST-03) | portal `ListShortcuts`, else Hyprland IPC binds; may be unreadable on Lua configs (`Degraded`, DEV-14) | portal | portal | direct | portal | direct |
| Tray (SH-07, CR-21) | StatusNotifierItem (bar) | SNI | native | SNI or XEmbed through Qt | `NeedsUserAction`: AppIndicator extension (DEV-26) | `NSStatusItem` |
| Background clipboard | `ext_data_control_v1` [GATE G4] | data-control | data-control | selection owner | `Degraded`: only while focused (DEV-26) | pasteboard |
| Floating panels: pins, thumbnails, toasts, history on top of every workspace (ED-05, ED-07, ED-08, ED-11) | layer-shell | layer-shell | layer-shell | above + sticky hints | `Degraded`: normal windows, no placement or on-top guarantee (DEV-26) | panels |
| Auto-scroll (CR-05) | virtual pointer + pointer warp | virtual pointer | `Unavailable` (DEV-25) | XTest | `Unavailable` (DEV-25) | CGEvent (`NeedsUserAction` until Accessibility is granted) |
| Screen recording (CR-07 onwards) | image-copy DMA-BUF or ScreenCast + PipeWire [GATE G6] | image-copy or ScreenCast | ScreenCast with restore token | XShm / XComposite | ScreenCast; picker each time unless a token restores (`Degraded`, DEV-26) | ScreenCaptureKit |
| Cursor telemetry (CR-15) | image-copy cursor session | ScreenCast cursor metadata where offered | ScreenCast metadata | XFixes | ScreenCast metadata | CGEvent |
| Click and key telemetry, live click ring and keystroke pill (CR-15, CR-17, CR-18) | `NeedsUserAction`: opt-in input helper (DEV-24) | same | same | XInput2 | same | event taps (`NeedsUserAction` until Input Monitoring is granted) |
| Self-exclusion of HUD and outline (CR-23) | `Degraded`: region recordings draw them outside the region; full-screen recordings move the HUD to another output or hide it (DEV-22) | same | same | same | same, stop through hotkey or notification action | ScreenCaptureKit exclusion list |
| Live webcam bubble in editable-mode recordings (CR-19) | `Degraded`: hidden over the recorded region (DEV-23) | same | same | same | same, no placement or on-top guarantee | excluded from capture |
| Share, Quick Look, Open With (OV-28, ED-06) | Share `Unavailable`; Quick Look → pin-style preview; Open With → OpenURI chooser (DEV-18) | same | same | same | same | native |
| Studio wallpaper background (VE-02) | current wallpaper where a source is found, else `Unavailable` (DEV-19) | same | Plasma wallpaper configuration | per window manager, often `Unavailable` | `org.gnome.desktop.background` | macOS system wallpapers |
| Focus return after capture | Hyprland IPC `focuswindow` | compositor default (`Degraded`, DEV-35) | compositor default (`Degraded`, DEV-35) | EWMH `_NET_ACTIVE_WINDOW` | compositor default (`Degraded`, DEV-35) | `NSRunningApplication` |
| Launch at login (ST-06) | XDG autostart | XDG autostart | XDG autostart | XDG autostart | XDG autostart | `SMAppService` |

The ML rows (`TextRecognition`, `BackgroundRemoval`, `OfflineTranslation`, `Captions`, …) depend on the platform and
on installed models, not on the desktop; they are specified in [09](09-ml-services.md).

## 3. Global hotkeys

### 3.1 Slots and storage

MacShot has 12 global hotkey slots with fixed identities and defaults, and a per-slot disabled flag
(`macshot/Services/HotkeyManager.swift:12-104`, `:60-62`). The slot table, labels and macOS defaults are generated
into the parity ledger; they are not copied here. `app::HotkeyService` owns the table at run time.

- Each slot has a stable action ID used for every backend: `capture-area`, `capture-screen`, `record-area`,
  `record-screen`, `history`, `capture-ocr`, `quick-capture`, `scroll-capture`, `open-from-clipboard`,
  `capture-last-area`, `pin-from-clipboard`, `clear-history`. The description shown to the desktop is the slot's
  translated MacShot label.
- A chord is stored as a platform-neutral accelerator in `QKeySequence::PortableText` form: Qt's `Ctrl` is ⌘ on macOS
  and Ctrl on Linux, `Meta` is ⌃ on macOS and Super on Linux. The stored value distinguishes three cases: unset (the
  platform default applies), explicitly unbound, and a chord. MacShot's keycode 0 is ambiguous with Carbon's `A` key,
  so AriadShot never uses a numeric sentinel. The settings format is in [07](07-storage-history-settings.md).
- Chords need at least one modifier, except function keys F1–F20, which may be bare
  (`macshot/Services/HotkeyManager.swift:120-126`).
- Only slots whose feature exists in the running build and milestone are registered. A slot for an absent feature is
  neither registered nor shown in the Shortcuts tab or the menus.
- When a hotkey fires while an AriadShot modal dialog is open, the dialog is closed before the action runs, as MacShot
  stops the modal session first (`macshot/Services/HotkeyManager.swift:175-178`).
- Every activation enters the command dispatcher as a capture-class or non-capture request ([§4](#4-request-queues-and-capture-availability)).

### 3.2 Backends, in order of preference

| Session | Backend | Class | Notes |
| :--- | :--- | :--- | :--- |
| Any Wayland desktop whose portal frontend exposes `org.freedesktop.portal.GlobalShortcuts` (Hyprland through `xdg-desktop-portal-hyprland`, KDE Plasma 6, GNOME 48+) | GlobalShortcuts portal | `backends::portal::GlobalShortcutsPortal` | Needs an application ID: the installed desktop file `io.github.bavanchun.AriadShot.desktop`, or registration through the portal's host application registry (`org.freedesktop.host.portal.Registry`) where offered. A build that is neither installed nor registered cannot bind; the Desktop Integration page says so and offers the CLI route |
| Hyprland without the portal interface | `hyprland_global_shortcuts_v1` on AriadShot's own connection | `backends::wayland::hyprland::HyprlandShortcuts` | Registers *named* actions (`ariadshot:<action-id>`); the protocol carries no key, the user's Hyprland configuration assigns it |
| X11 | `XGrabKey` on the root window | `backends::x11` | Grabs every Lock/NumLock variant of the chord; a `BadAccess` conflict shows an error pill and leaves the slot unset |
| macOS | Carbon `RegisterEventHotKey` | `backends::macos::CarbonShortcuts` | As MacShot; needs no permission |
| Everywhere | the `ariadshot` CLI bound in the compositor or desktop configuration | [§5](#5-single-instance-control-socket-cli-and-url-scheme) | Universal fallback; also the documented route on compositors without the portal |

Portal protocol ([GlobalShortcuts](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html)):
one session per daemon (`CreateSession`), `BindShortcuts` with every registered action ID, its description and a
`preferred_trigger` when the user recorded one, `ListShortcuts` for read-back, the `Activated` signal for dispatch and
`ShortcutsChanged` to refresh labels. The session is recreated when the portal frontend restarts.

### 3.3 Recording a chord: request, then read back (ST-03, DEV-14)

MacShot records a chord and registers it immediately. On Wayland the compositor or the portal owns the key, so the
Shortcuts-tab recorder becomes "request, then read back":

| Backend | What "Set" does | What the Shortcuts row and the SH-09 menu labels show |
| :--- | :--- | :--- |
| GlobalShortcuts portal | Records the chord as MacShot's recorder does, then calls `BindShortcuts` with it as `preferred_trigger`; the desktop may show its own dialog | the portal's `trigger_description` from `ListShortcuts`; "(requested: <chord>)" while it is unset or differs |
| Hyprland (portal or protocol) | After recording, offers **Apply in Hyprland**: writes the managed file `~/.config/hypr/ariadshot-bindings.lua` and shows the single `require` line the user adds to their own bindings file; the snippet can also be copied. AriadShot never edits the user's own configuration files | the trigger read back from Hyprland's IPC socket (the data `hyprctl binds -j` prints) for binds whose dispatcher is `global` with argument `ariadshot:<action-id>`. Lua-configured setups can report such binds with the dispatcher `__lua`; then the row says "Bound in Hyprland config (chord not readable)" |
| X11 | `XGrabKey` directly | the grabbed chord |
| macOS | Carbon, as MacShot | the chord |

- The managed file contains one bind per registered slot that has a chord. Its syntax follows the running Hyprland
  version and is verified in M1; a setup configured through `hyprland.conf` instead of Lua gets the equivalent file
  sourced with one `source =` line. The file carries a header comment saying it is generated and overwritten.
- **Clear** unregisters the slot and sets its disabled flag (MacShot's `hotkeyDisabled_<slot>`). **Reset** restores the
  platform default table.
- **Conflict policy.** AriadShot never steals a chord silently. The portal or the compositor decides; AriadShot shows
  the result. On Hyprland the row lists existing binds that use the same chord, read from the IPC socket.

### 3.4 Default chords

- **macOS:** MacShot's defaults, from the ledger.
- **Linux (DEV-13):** only Capture Area is requested by default, as Ctrl+Shift+4, and only when the user accepts it
  in the first-run window or the Shortcuts tab. Capture Screen (Ctrl+Shift+3) and Record Area (Ctrl+Shift+5) are
  offered as suggestions and stay unbound until accepted. The other nine slots are unbound. The Linux table follows
  the macOS screenshot convention (⌘⇧3, ⌘⇧4, ⌘⇧5) because MacShot's ⌘⇧ letters land on common application chords
  on Linux (Ctrl+Shift+T reopens a tab, Ctrl+Shift+R hard-reloads, Ctrl+Shift+F finds in files, Ctrl+Shift+S is
  Save As, Ctrl+Shift+H replaces), and Super+Shift is taken by common compositor configurations.

### 3.5 Menu labels (SH-09)

Status-menu items show the chord that is actually bound: the read-back trigger on Wayland, the grabbed chord on X11,
the registered chord on macOS. A disabled, unbound or unreadable slot shows no label, as MacShot hides labels for
disabled hotkeys (`macshot/Services/HotkeyManager.swift:338-378`). Labels are rebuilt when the keyboard layout changes
([§14](#14-keyboard-layouts-and-layout-aware-shortcuts)) and when the portal reports `ShortcutsChanged`.

## 4. Request queues and capture availability

### 4.1 Two queues (SH-03)

MacShot keeps two request queues (`macshot/AppDelegate.swift:2388-2436`):

1. **Launch queue.** Every request (URL, file open, CLI, hotkey) that arrives before the launch sequence finishes is
   held and drained in order at its end ([01 §9](01-architecture-overview.md)).
2. **Capture queue.** Capture-class requests are held while capture is not yet available and replayed in order when it
   becomes available. The capture class is MacShot's set: `capture`, `capture-fullscreen`, `capture-last`,
   `quick-capture`, `ocr`, `ocr-translate`, `record`, `record-fullscreen`, `scroll-capture`
   (`macshot/AppDelegate.swift:2432-2435`), plus the hotkey and menu equivalents.

Non-capture actions (`settings`, `history`, `open`, `edit`, `stop-recording`, the clipboard actions) always run
immediately, even while the first-run window is open.

### 4.2 Capture availability states

On macOS the state is MacShot's: the Screen Recording permission. On Linux it comes from the capability registry:

| `CaptureAvailability` | Entered when | Queued capture-class requests |
| :--- | :--- | :--- |
| `Probing` | start-up, before the registry resolves; bounded at 2 s | held; drained in order when the state resolves |
| `Available` | image-copy, screencopy, XShm, or a portal with a valid grant | run immediately |
| `ConsentPending` | a portal dialog is open for the first capture | the first request waits for the dialog; later ones are held |
| `Denied` | the user cancelled or denied the portal dialog | **dropped** with an error pill and a CLI result |
| `Unavailable` | no capture backend works | **dropped** with an error pill and a CLI result |

- A dropped request shows an error pill in MacShot's error-pill style (OV-27) with the text "Capture isn't available:
  <reason>", and the CLI receives `capture-unavailable` ([§5.3](#53-actions-codes-and-exit-status)). Nothing is replayed
  later, which is MacShot's intent when it clears the queue (`macshot/AppDelegate.swift:404-410`).
- Closing the first-run window while the state is `Denied` or `Unavailable` drops every held request, as closing
  MacShot's onboarding window does. If capture became available while the window was open, closing it follows the
  available path and drains the queue.
- **Actions absent from the running build.** During M1–M4 some MacShot actions do not exist yet ([14](14-gates-and-milestones.md)).
  Such an action, requested through a URL or the CLI, shows an error pill "“<action>” isn't available in this version
  of AriadShot" and returns `not-available` to the CLI. It is never ignored silently.
- **Capture gate** (SH-13). A capture-class request that arrives while a capture or recording is active is ignored, as
  MacShot ignores it; the CLI receives `busy`. No UI is shown.

## 5. Single instance, control socket, CLI and URL scheme

### 5.1 Single instance (SH-01)

- The daemon owns a per-user runtime directory: `$XDG_RUNTIME_DIR/ariadshot/` on Linux; on macOS and on Linux
  sessions without `XDG_RUNTIME_DIR`, `$TMPDIR/ariadshot-<uid>/` (falling back to `/tmp/ariadshot-<uid>/`). The
  directory is created with mode 0700; if it exists with another owner or wider permissions, the daemon refuses to
  start and logs why ([11 §5](11-security-privacy.md)).
- The first daemon takes `daemon.lock` in that directory with `QLockFile` and listens on `ctl.sock` with
  `app::ControlServer` (`QLocalServer`, socket options restricting access to the user).
- A second `ariadshot-daemon` fails to take the lock, connects to `ctl.sock` (retrying for up to 2 s while the first
  instance starts), forwards its launch and exits with status 0:
  - without arguments it sends `activate`: the primary makes the tray icon visible again (clearing `hideMenuBarIcon`)
    and opens Settings, as MacShot's duplicate launch does (`macshot/AppDelegate.swift:262-273`, `:1667-1672`);
  - with arguments (files or `ariadshot://` URLs from the desktop entry) it sends one `open` or `url` request per
    argument instead.
- Tests isolate instances through temporary `XDG_RUNTIME_DIR`/`TMPDIR` directories; no other instance namespace
  exists.

### 5.2 Control protocol

Line-delimited JSON over the local socket: one UTF-8 JSON object per line, terminated by `\n`, at most 64 KiB per
line. A client may send several requests on one connection; the server answers each with exactly one response line,
in request order.

Request:

```json
{"v": 1, "id": "c7f1", "action": "ocr-translate", "args": {"target": "zh-CN"}}
```

| Field | Type | Rule |
| :--- | :--- | :--- |
| `v` | integer | protocol version; `1`. Another value gets `bad-request` |
| `id` | string, 1–64 characters | chosen by the client, echoed in the response |
| `action` | string | one of §5.3 |
| `args` | object | action arguments; unknown keys are rejected with `bad-args` |

Response:

```json
{"v": 1, "id": "c7f1", "ok": false, "code": "capture-unavailable", "message": "Capture isn't available: the portal request was denied"}
```

| Field | Type | Rule |
| :--- | :--- | :--- |
| `ok` | boolean | `true` only for `code` `ok` |
| `code` | string | §5.3 |
| `message` | string | empty on success; otherwise the same translated text the UI shows, or a CLI-only explanation when the UI shows nothing |

The response is sent when the request leaves the dispatcher: immediately for a request that runs, and after the
queue resolves for a held one ([§4](#4-request-queues-and-capture-availability)). "Runs" means the action started; the
CLI does not wait for a capture to finish, matching MacShot's fire-and-forget URL actions. Malformed JSON gets one
`bad-request` response and the server closes the connection. The server checks the peer's user ID where the
platform provides it (`SO_PEERCRED`, `getpeereid`) and closes connections from any other user.

### 5.3 Actions, codes and exit status

Actions mirror MacShot's URL actions (SH-04, `macshot/AppDelegate.swift:2438-2469`) plus two daemon-internal ones:

| Action | Args | Capture class | Behaviour |
| :--- | :--- | :-: | :--- |
| `capture` | — | yes | Capture Area |
| `capture-fullscreen` | — | yes | Capture Screen |
| `capture-last` | — | yes | Capture Last Area (SH-21) |
| `quick-capture` | — | yes | Quick Capture (SH-18) |
| `ocr` | — | yes | Capture OCR & QR (SH-19) |
| `ocr-translate` | `target` (optional language code) | yes | OCR, translate, draw in place (SH-20). The target is trimmed; an omitted or empty target uses the saved target language, as in MacShot (`macshot/AppDelegate.swift:2444-2449`). Any other value is passed to the translator unchanged: the request answers `ok` when the action starts, and an unusable code fails later with the translator's own error, never with `bad-args` |
| `record`, `record-fullscreen` | — | yes | Record Area, Record Screen |
| `scroll-capture` | — | yes | Scroll Capture |
| `settings`, `history` | — | no | open Settings; show the history panel |
| `open` | `file` (absolute path) | no | the file router of §5.6 |
| `edit` | `id` (history item ID) | no | open the item in the editor with editable annotations when available, else the flattened image |
| `stop-recording` | — | no | stop an active recording; no effect otherwise |
| `url` | `url` (an `ariadshot://` URL) | per action | decoded exactly as a URL-scheme delivery (§5.5) |
| `activate` | — | no | the duplicate-launch behaviour of §5.1 |

| Code | Meaning | CLI exit status |
| :--- | :--- | :-: |
| `ok` | the action ran (or, for `stop-recording`, nothing needed stopping) | 0 |
| `internal` | an unexpected failure; details in the daemon log | 1 |
| `bad-request`, `unknown-action`, `bad-args` | the request is malformed | 2 |
| — | the CLI could neither reach nor start the daemon, or got no response in time | 3 |
| `not-available` | the action does not exist in this build ([§4.2](#42-capture-availability-states)) | 4 |
| `capture-unavailable` | capture is `Denied` or `Unavailable` | 5 |
| `busy` | the capture gate is closed | 6 |
| `not-found` | the file cannot be read or the history ID is unknown | 7 |

`busy` and `not-found` show no UI, as MacShot shows none for those cases; only the CLI reports them.

### 5.4 The `ariadshot` CLI

`src/cli/` builds a client with no Qt linkage ([02 §15](02-modules-and-interfaces.md)).

```text
ariadshot capture | capture-fullscreen | capture-last | quick-capture | ocr | record | record-fullscreen
          | scroll-capture | settings | history | stop-recording
ariadshot ocr-translate [--target <language>]
ariadshot open <file>          # relative paths are resolved against the CLI's working directory
ariadshot edit <history-id>
ariadshot --help | --version
options: --timeout <seconds>   # wait for the response (default 60)
```

- It builds one request, connects to `ctl.sock`, and prints `message` to standard error when `ok` is false. Its exit
  status is the table above.
- **Daemon auto-start.** When no socket answers, the CLI starts `ariadshot-daemon` (from its own directory, else
  `PATH`) detached from the terminal with its standard streams on `/dev/null`, waits up to 5 s for the socket
  [HYPOTHESIS: start-up bound measured at G8], then sends the request. Capture requests then pass through `Probing`.
- The CLI is the recommended target for compositor key binds on desktops without the GlobalShortcuts portal, and for
  scripts. It is not gated by `urlSchemeEnabled`.

### 5.5 URL scheme (SH-04)

- Scheme `ariadshot`, action in the host part, parameters in the query: `ariadshot://capture`,
  `ariadshot://ocr-translate?target=zh-CN`, `ariadshot://open?file=/abs/path.png`, `ariadshot://edit?id=<id>`.
  Actions and parameters are those of §5.3 except `url` and `activate`.
- `urlSchemeEnabled` (default `true`) gates every URL: when it is off, URLs are ignored without UI, as in MacShot
  (`macshot/AppDelegate.swift:2402-2403`). Unknown actions are ignored.
- Registration: on Linux the desktop entry declares `MimeType=x-scheme-handler/ariadshot;` and
  `Exec=ariadshot-daemon %U`, so the desktop launches the daemon with the URL, and the single-instance forwarding of
  §5.1 delivers it. On macOS `CFBundleURLTypes` registers the scheme and URLs arrive through the application delegate.
- URL parameters are untrusted input and are validated before dispatch ([11](11-security-privacy.md)).

### 5.6 File routes (SH-30)

- Entry points: Open With and default-application launches (desktop entry `%U`), command line, CLI `open`,
  `ariadshot://open?file=`, Open Image…, Open Video…, and drag onto AriadShot windows.
- **Router** (`macshot/AppDelegate.swift:2396-2425`): extensions `png jpg jpeg tiff tif bmp gif heic heif webp icns`
  open in the image editor; `mp4 mov m4v` open in the studio (from M4); anything else is ignored. **GIF deliberately
  opens in the image editor**; users who want to trim a GIF use Open Video….
- Registration: the desktop entry lists MacShot's eight image document types as `MimeType=`
  (`image/png;image/jpeg;image/tiff;image/gif;image/heic;image/heif;image/webp;image/bmp;`), matching
  `CFBundleDocumentTypes` with role Editor and rank Alternate on macOS (`macshot/Info.plist:24-45`). Video types are
  not registered, as in MacShot.
- A video opened through a route is a non-take video: the studio opens it without telemetry, with the cursor,
  zoom-from-clicks and keystroke looks disabled and the reason shown ([05](05-recording-and-studio.md)).
- MacShot's URL `open` action always uses the image path (`macshot/AppDelegate.swift:2457-2461`), so a video path fails
  there silently. AriadShot applies the router to it when the video route lands in M4 (DEV-46).
- HEIC and HEIF decode on Linux only where an image plugin is installed; a file that cannot be decoded is ignored, as
  in MacShot (`macshot/AppDelegate.swift:2313-2325`), and the CLI receives `not-found`.

## 6. Tray, status item and status menu

- **Linux:** `QSystemTrayIcon` (StatusNotifierItem plus dbusmenu) through `app::TrayController`. **macOS:**
  `backends::macos::StatusItemMac` (`NSStatusItem`, 22 × 22 pt template image), because `QSystemTrayIcon` cannot
  switch the item into MacShot's click-to-stop mode (SH-07, `macshot/AppDelegate.swift:717-800`).
- **Icon.** AriadShot's own template icon (DEV-01). MacShot's custom-symbol mode (`statusBarIconMode` with an SF
  Symbol name) exists on macOS only; on Linux its settings row is hidden because SF Symbols cannot be used there
  (DEV-02). `hideMenuBarIcon` hides the item.
- **Recording mode** (`macshot/AppDelegate.swift:3000-3025`): the item is forced visible even when hidden by the user,
  its image becomes the stop glyph (`stop.circle.fill` on macOS, the matching glyph of the Linux icon set), its menu is
  detached, and a primary click stops the recording. When recording ends the user's visibility preference, icon and
  menu return.
- **No tray host** (no `org.kde.StatusNotifierWatcher`, GNOME without the AppIndicator extension): the daemon keeps
  running; hotkeys, the CLI and a second launch (which opens Settings) remain the entry points, and the Desktop
  Integration page shows the fix (`Tray` is `NeedsUserAction`).
- **Status menu** (SH-08, `macshot/AppDelegate.swift:813-943`): the six reorderable capture items in the order stored
  in `captureMenuItemOrder` (`macshot/AppDelegate.swift:10-80`); Capture Delay with None, 3, 5, 10 and 30 seconds
  (`macshot/AppDelegate.swift:827`); Record Area and Record Screen; Recent Captures (rows "W × H · time ago" with a
  thumbnail, a click copies the entry, Clear History with a confirmation, "No recent captures" when empty); Show
  History Panel; Open Image…, Open Video…, Show Recordings in the file manager (SH-34); Open from Clipboard, Pin from
  Clipboard; Settings…; Check for Updates…; Quit. The ledger holds the full item list, symbols and separators.
- Items exist only when their feature exists in the running build ([14 §4](14-gates-and-milestones.md)).
- **Labels that name macOS services** ("Show Recordings in Finder") use a platform-neutral term on Linux; each such
  string difference is part of the substitution it belongs to (SH-34, DEV-18).
- **Quit** exits the daemon. In ordinary windows Ctrl+Q closes the window, not the daemon ([§10](#10-linux-substitutes-for-macos-services)).

## 7. Clipboard

`platform::ClipboardService` has one write path and one read path; the caller states the copy context.

### 7.1 Writing

- **What is written** (SV-03, `macshot/Services/ImageEncoder.swift:207-278`): the chosen export format when
  `clipboardIncludesImageFormat` is on and the format is not PNG, then PNG always, then TIFF on macOS only. The bytes
  are those of a file export of the same canonical image ([03 §2](03-rendering-contracts.md)). No file URL is offered.
- **Encoding** runs off the GUI thread. A monotonic clipboard generation counter is taken when the copy is requested;
  before publishing, the service checks that no newer copy was requested and that another client has not taken the
  selection since, and otherwise discards its result, as MacShot guards against stomping concurrent copies.
- **Focused copies** use `QClipboard` while an AriadShot surface has focus (a Wayland selection needs a focused
  surface and an input serial). The confirm route copies before the overlay is dismissed.
- **Background copies** (a Recent Captures click, "When done: Copy to clipboard" after a recording, a history card
  after the panel closed) use `ext_data_control_v1` on AriadShot's own connection through
  `backends::wayland::DataControlClipboard` on Hyprland, wlroots and KDE [GATE G4]; the X11 selection owner; and
  `NSPasteboard` on macOS.
- **Serving.** On Wayland and X11 the owner must stay alive to serve pastes. The daemon keeps the encoded bytes of the
  current selection (never the pixels) until another client takes the selection, and writes them to each requesting
  file descriptor on the `WaylandSession` thread. These bytes count against the idle memory budget
  ([12](12-testing-strategy.md)).
- **GNOME** has no data-control protocol. Background copies are `Degraded` (DEV-26): the request is fulfilled through a
  focused AriadShot surface when one is visible; otherwise the result appears in the floating thumbnail, whose Copy
  pill (ED-05) performs a focused copy when clicked.

### 7.2 Reading

Open from Clipboard and Pin from Clipboard (ED-09) read in MacShot's priority order
(`macshot/Services/ClipboardPinService.swift:20-79`): image flavours (PNG, TIFF, JPEG, HEIC, HEIF, GIF, or a file URL
to an image), RTF, RTFD (macOS), sanitised HTML, plain text. The Linux MIME equivalents are `image/png`,
`image/tiff`, `image/jpeg`, `image/heic`, `image/heif`, `image/gif`, `text/uri-list`, `text/rtf`, `text/html` and
`text/plain;charset=utf-8`. Triggered from a hotkey or the tray, the read uses data-control where available; on GNOME
it needs a focused surface and is `Degraded`. The HTML and RTF parsers and their bounds are in
[11](11-security-privacy.md).

## 8. Desktop portals

`backends::portal::PortalSession` implements the XDG desktop portal request pattern once for every portal:

- Every call passes a `handle_token`, subscribes to the `Response` signal of the predicted request object path before
  calling, and maps the response code: `0` success, `1` cancelled by the user (→ `Denied` for capture), `2` other
  failure.
- `parent_window` is the exported identifier of the focused AriadShot window when one exists (`wayland:<handle>` via
  `xdg-foreign`, `x11:<xid>`), otherwise empty.
- Sessions (ScreenCast, GlobalShortcuts) watch the `Closed` signal and are recreated on demand; a vanished portal
  frontend moves the dependent capabilities to `Unavailable` until it returns.

Per portal:

| Portal | Use | Rules |
| :--- | :--- | :--- |
| GlobalShortcuts | hotkeys | [§3](#3-global-hotkeys) |
| Screenshot | GNOME stills | `interactive=false` is only a hint; the backend may still show a dialog. If it does, the flow becomes "portal dialog, then AriadShot's overlay on the returned image" (DEV-26). A whole-desktop image is split by output geometry ([04 §4](04-capture-and-overlay.md)) |
| ScreenCast | recording on KDE and GNOME, and on Hyprland when G6 selects it | `persist_mode` 2; the `restore_token` returned by every successful `Start` replaces the stored one immediately (tokens are single-use chains); a token may be ignored after output or permission changes, in which case the picker shows again. Cursor mode is chosen from `AvailableCursorModes`. Streams are targeted by `pipewire-serial` (`PW_KEY_TARGET_OBJECT`) on version 6 and later, by node ID before that ([ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)) |
| OpenURI | Open With (`ask: true`), opening links (QR rows, AI Search), revealing a folder | `writable: false`; files are passed as file descriptors (`OpenFile`) |
| FileChooser | Save As…, Open Image…, Open Video… in sandboxed builds | `QFileDialog` decides the route outside Flatpak; the Flatpak build always uses the portal |
| Notification | the recording stop action on desktops without a tray | only while recording on such desktops |

Restore tokens and other portal state live in the daemon's state directory ([07](07-storage-history-settings.md)),
never in the settings document or its export.

## 9. macOS integration

`backends/macos/` implements every interface on macOS with Objective-C++ and, for Translation, a small Swift module.
This shim is feature-sized work with its own gates ([14 §3](14-gates-and-milestones.md#3-platform-feature-gates)).

| Area | Behaviour | MacShot source |
| :--- | :--- | :--- |
| Overlay panels | borderless `NSPanel`, `.nonactivatingPanel`, level 257, collection behaviour `canJoinAllSpaces` + `fullScreenAuxiliary`; the scroll-capture HUD at level 258. The panel becomes key without activating AriadShot, and the previous application keeps its active state | `macshot/UI/Overlay/OverlayWindowController.swift:165-184`, `macshot/UI/Overlay/ScrollCaptureHUDView.swift:134` |
| Exact colours | the panel host presents un-dithered 8-bit backing stores, as MacShot disables automatic layer backing stores at start-up | `macshot/main.swift:9` |
| Status item | §6 | `macshot/AppDelegate.swift:717-800`, `:3000-3025` |
| Hotkeys | Carbon (§3) | `macshot/Services/HotkeyManager.swift` |
| Capture | ScreenCaptureKit, sequential per display, pointer display first ([04](04-capture-and-overlay.md)) | `macshot/Capture/ScreenCaptureManager.swift` |
| Permissions | Screen Recording (preflight polling every 0.75 s in the onboarding window, SH-22), Microphone, Camera, Accessibility (auto-scroll, element snapping), Input Monitoring (keystrokes), Speech (captions); usage strings rewritten for AriadShot | `macshot/UI/Windows/PermissionOnboardingController.swift`, `macshot/Info.plist:46-53` |
| Launch at login | `SMAppService.mainApp` register and unregister; re-applied after a settings import | `macshot/AppDelegate.swift:782-790` |
| Main menu (SH-31) | App (About, Quit ⌘Q), File (Close Window ⌘W), Edit (Undo and Redo with the dynamic chords, Cut, Copy, Paste, Delete, Select All) | `macshot/AppDelegate.swift:672-714` |
| Accessory app (SH-05) | no Dock icon; a regular app while an editor, the studio or Settings is open; accessory again when they close | `macshot/AppDelegate.swift:1035-1078` |
| Focus return | on close, activate the previous application (`yieldActivation` then `activate` on macOS 14+); never hide the app, because hiding suspends the Carbon event dispatcher and breaks hotkeys | `macshot/AppDelegate.swift:1035-1078` |
| Dock menu (SH-06) | titled windows sorted alphabetically, miniaturised ones marked mixed | `macshot/AppDelegate.swift:526-555` |
| App Nap (SH-29) | `beginActivity(.userInitiatedAllowingIdleSystemSleep)` for hotkey responsiveness; a SIGTERM diagnostic log | `macshot/AppDelegate.swift:285-299` |
| Move to Applications | when launched from a disk image or a translocated path, offer to move the app to `/Applications` (with "Don't ask again") | `macshot/AppDelegate.swift:574-619` |
| URL scheme and documents | `CFBundleURLTypes` (`ariadshot`), `CFBundleDocumentTypes` (§5.6) | `macshot/Info.plist:5-45` |
| Updates | Sparkle 2 with EdDSA only ([13 §7](13-build-ci-release.md)) | `macshot/Info.plist:54-63` |

Single instance on macOS also uses the control socket (§5.1) rather than MacShot's distributed notification, so one
mechanism serves both platforms.

## 10. Linux substitutes for macOS services

| MacShot | Linux behaviour | Deviation |
| :--- | :--- | :--- |
| Share sheet (OV-28, ED-04, ED-06) | hidden; upload, copy and drag-out remain | DEV-18 |
| Quick Look (history Space, ED-06) | the pin-style preview at fit size, closed by Space or Esc | DEV-18 |
| Open With (ED-06) | OpenURI portal `OpenFile` with `ask: true`, so the desktop shows its application chooser; without the portal, `xdg-open` semantics through `QDesktopServices` | DEV-18 |
| Show in Finder (SH-34, CR-24) | `org.freedesktop.FileManager1.ShowItems` over D-Bus; without it, the OpenURI portal's `OpenDirectory` method on the item, which opens its containing folder | DEV-18 |
| Studio `.wallpaper` background (VE-02) | `platform::WallpaperSource` reads the current wallpaper where the desktop exposes it; otherwise the option is hidden | DEV-19 |
| Standard main menu (SH-31) | each window wires Ctrl+W (close), Ctrl+Q (MacShot's ⌘Q, which closes the editor or Settings window, not the daemon) and the edit chords itself | DEV-12 |
| Accessory app and Dock menu (SH-05, SH-06) | ordinary toplevels while windows are open; no Dock menu | DEV-35 |
| Focus return | Hyprland IPC `focuswindow` on the window focused before the capture; EWMH on X11; the compositor's default elsewhere | DEV-35 ([§2](#2-capability-matrix)) |
| App Nap, SIGTERM log (SH-29) | no App Nap; SIGTERM is a clean quit; fatal signals append a one-line record to `$XDG_STATE_HOME/ariadshot/logs/crash.log` ([11 §7](11-security-privacy.md)) | DEV-47 |
| Sparkle updates (SH-26) | "Check for Updates…" checks the release feed and points to the package manager | DEV-36 ([10](10-upload-and-network.md)) |

## 11. Desktop Integration page and first-run window

The Linux Settings window adds a **Desktop Integration** page, which replaces MacShot's Screen Recording onboarding
(DEV-30). It lists every capability of [§1](#1-capability-registry) that is not `Available`, with its state, reason
and fix, and also:

- registers hotkeys and shows each slot's bound trigger ([§3](#3-global-hotkeys));
- on Hyprland, offers the managed bindings file and shows the `require` line;
- where the Omarchy command `omarchy-capture-screenshot` is on `PATH`, offers to install a shim of that name in
  `~/.local/bin` that calls `ariadshot capture`, so the desktop's own screenshot binds reach AriadShot without
  editing the compositor configuration. The shim works only if `~/.local/bin` precedes the directory of the original
  command in the compositor's `PATH`. The page checks this by resolving the command name through the daemon's own
  `PATH` (inherited from the session when the daemon was started by autostart or the compositor): it reports the shim
  as active only when the name resolves to the shim, as shadowed (with the `PATH` order to fix) when it resolves to
  the original, and as unverifiable when the daemon was started from a terminal. The shim is never written over an
  existing file and is removed by the same page;
- lists on-device models with status, size, licence, delete and re-download ([09](09-ml-services.md));
- explains the opt-in input helper, which the user installs knowingly (a udev rule or `input` group membership), and
  what it can see ([11 §6](11-security-privacy.md));
- shows the credential storage state (keyring or the degraded file store, [11 §4](11-security-privacy.md)).

**First-run window.** On Linux the first launch shows a window with MacShot's onboarding geometry and tone (400 × 520
pt; `macshot/UI/Windows/PermissionOnboardingController.swift:40-52`) and Linux content: the capture state of
[§4.2](#42-capture-availability-states), the Capture Area chord offer of [§3.4](#34-default-chords), launch at login, and
a link to the Desktop Integration page. Closing it applies the queue rule of §4.2. On macOS the window is MacShot's
onboarding (SH-22).

The Desktop Integration page arrives in M2 ([14 §4](14-gates-and-milestones.md)). Until then the first-run window is
the only capability surface: it lists the capabilities M1 uses (capture, overlay, global shortcuts, tray, background
clipboard, launch at login) with state, reason and fix text, and every "How to enable" link opens it. From M2 its link and the "How to enable" links point to the Desktop Integration
page.

Strings that exist only on Linux are AriadShot-authored, English first, and translated through the project's own
translation process; they fall back to English until translated.

## 12. Launch at login (ST-06)

- **Linux:** enabling the setting writes `$XDG_CONFIG_HOME/autostart/io.github.bavanchun.AriadShot.desktop` with
  `Exec=ariadshot-daemon`; disabling it deletes that file. There is no systemd user unit and no `X-systemd-skip` key,
  which would suppress the entry on sessions that run XDG autostart through systemd's
  `xdg-desktop-autostart.target`. The setting's displayed state is read from the file's presence.
- **macOS:** `SMAppService.mainApp` (§9).
- The daemon started at login opens no window.

## 13. Capture sound (SH-28)

A capture sound plays after a copy or save when `playCopySound` is on (default `true`,
`macshot/AppDelegate.swift:2083-2084`). AriadShot ships its own sound asset on every platform (DEV-01); MacShot plays
the system's screen-capture sound. The sound is loaded at launch so the first playback has no start-up delay, as
MacShot pre-warms its audio output (`macshot/AppDelegate.swift:333-339`). Playback uses a pre-loaded `QSoundEffect`.

## 14. Keyboard layouts and layout-aware shortcuts

Single-key overlay shortcuts (SH-11) and modifier chords (SH-12) match by the character the active layout produces,
with a Latin fallback for non-Latin layouts (SV-08), ported from `macshot/Services/KeyboardShortcutMatcher.swift:1-116`:

- A character is normalised by lower-casing; it must be one character and not a control or newline character.
- **Chords** (with modifiers): the relevant modifiers must match exactly (Ctrl, Shift, Alt, Meta on Linux; ⌘, ⇧, ⌥,
  ⌃ on macOS). The semantic character is the event's own character when that is a single ASCII character; otherwise
  the character the same physical key produces in the ASCII-capable layout; otherwise the event's character. Both
  sides must resolve to a character; a key that produces none (Return, Escape, Tab) never matches a character binding.
- **Single keys** (no modifiers): the candidates are the event's own character, plus the ASCII-capable layout's
  character for the same key when the first is not ASCII. A binding matches either candidate, so Latin defaults keep
  working under Cyrillic, Arabic or other layouts.
- **ASCII-capable layout.** macOS uses the current ASCII-capable input source, as MacShot does. Linux translates the
  event's hardware keycode with xkbcommon through the first Latin layout of the session keymap; the keymap comes from
  `wl_keyboard.keymap` on AriadShot's own Wayland connection, or from XKB on X11. Verified in M1 with a Vietnamese,
  a Cyrillic and a CJK input configuration.
- **Layout changes** rebuild the status-menu labels and the Shortcuts tab, as MacShot does on input-source change
  (`macshot/AppDelegate.swift:1675-1683`). Linux observes new keymaps on its own connection; macOS observes the
  input-source notification.

## 15. Display, workspace, sleep and power events

| Event | Behaviour | MacShot source |
| :--- | :--- | :--- |
| Output added, removed or changed | when not capturing or recording: rebuild the overlay pool (SH-15), re-resolve capabilities; "Capture Last Area" keeps applying only to an output whose frame matches exactly (SH-21) | `macshot/AppDelegate.swift:476-480` |
| Workspace or Space switch | an open capture overlay is dismissed. Linux uses the backend's workspace events where they exist (Hyprland IPC event socket, EWMH `_NET_CURRENT_DESKTOP`) | `macshot/AppDelegate.swift:1683-1686` |
| Wake from sleep | when not capturing or recording: re-probe and pre-warm the capture path | `macshot/AppDelegate.swift:471-474` |
| Sleep | an active recording stops before sleep (CR-12). Linux takes a logind `delay` inhibitor while recording and stops on `PrepareForSleep(true)`, releasing the inhibitor once the writer has finalised or logind's delay expires; macOS observes the will-sleep notification | `macshot/Capture/RecordingEngine.swift:291-296` |
| Idle during recording | idle sleep is inhibited while recording (logind `idle` inhibitor on Linux; a user-initiated activity with idle sleep disabled on macOS) | `macshot/Capture/RecordingEngine.swift:288-289` |
| SIGINT, SIGTERM | clean quit through `app::SignalBridge`; an active recording is finalised as on quit | — |

## 16. Localisation and runtime language switching (SH-27)

- 40 locales plus "System", about 858 keys, converted from MacShot's `Localizable.strings` catalogues to Qt `.ts`
  files with stable IDs and positional arguments. The converted catalogues are MacShot-derived material
  (GPL-3.0-only) and are listed in `PROVENANCE.md` ([13 §8](13-build-ci-release.md)).
- The setting `appLanguage` defaults to `system`. "System" resolves by walking the user's preferred languages and
  matching language and script (`zh-Hans-CN` → `zh-Hans`), as `macshot/Services/LanguageManager.swift` does.
- Switching language at run time installs the new `QTranslator` set (AriadShot's catalogue plus Qt's own), and
  `QEvent::LanguageChange` reaches every window and view object, which re-translate their strings without a restart.
  Layout direction follows the language: Arabic, Persian and Hebrew mirror the chrome and ordinary windows.
- M1 ships English only, but every user-visible string is `tr()`-wrapped from the first commit; the other locales
  arrive in M2.
