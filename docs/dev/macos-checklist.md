<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# macOS checklist

The maintainer runs this on the Mac for every slice that touches `render/`, `ui/`, `hosts/`, `windows/`,
`backends/macos/` or packaging, and at every milestone exit. Hosted CI covers the build and the offscreen tests on
every pull request; this checklist covers what only a real Mac shows.

## 1. Build and tests

After reading the pull request's diff:

```sh
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
scripts/macos/verify.sh <pull request head commit>
```

The script builds and tests that commit in a temporary worktree and prints a report table. Paste it into the pull
request, followed by the items below that apply, each marked passed, failed or not applicable, with a screenshot for
visual items.

## 2. Items that apply to the change

**Scaffold (current build)**

- [ ] `ariadshot-daemon --version` prints the version with the commit (`0.0.0+g<hash>` before the first release).
- [ ] The daemon starts; Ctrl+C in its terminal and `kill -TERM <pid>` end it with exit status 0.

**Permissions** (when capture, recording or input features change)

- [ ] The first capture asks for Screen Recording with AriadShot's text; the onboarding window behaves as MacShot's.
- [ ] Microphone, camera, Accessibility and Input Monitoring prompts appear only when their feature is used.
- [ ] After replacing the application bundle with a new build, note whether each granted permission still applies.

**Panels and windows** (overlay, thumbnail, pin, toasts)

- [ ] The overlay appears above full-screen applications on every Space, becomes key without activating AriadShot,
      and the previous application keeps its active state; Esc dismisses it.
- [ ] Thumbnail, pin and toasts stay on top where MacShot's do, and never show in the capture.

**Text input**

- [ ] A Vietnamese input source (Telex) and a CJK input source compose inline in the canvas text control, commit
      correctly, and undo as one step.

**Capture**

- [ ] The pointer's display freezes first; the other displays follow.
- [ ] Captures are correct on a Retina display and on an external display at a different scale.
- [ ] Window capture keeps the window's real alpha for beautify.

**Visual spot checks**

- [ ] The changed surfaces match the MacShot reference screenshots for the same state (toolbar, options row, popovers,
      resolution box, helper cards).
- [ ] SF Symbols, the system font and Apple Color Emoji are used on macOS.

**Status item and shortcuts** (when they change)

- [ ] The status item shows the template icon and the menu items of this build.
- [ ] Global shortcuts use MacShot's default chords and work while another application is active.

**Distribution** (packaging changes and milestone exits)

- [ ] `codesign --verify --deep --strict` passes on the ad-hoc-signed bundle.
- [ ] After a quarantined download, the first open is blocked and the documented steps open it (macOS 15 and later:
      System Settings → Privacy & Security → Open Anyway; macOS 14: Control-click → Open).

## 3. macOS 14

Until the macOS 14 VM runner exists, repeat section 1 on macOS 14 when the change affects APIs whose behaviour differs
between macOS 14 and 26, and record the macOS build numbers you tested.
