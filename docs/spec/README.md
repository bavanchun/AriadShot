<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# AriadShot Technical Specification

AriadShot is a faithful Linux and macOS remake of MacShot
([github.com/sw33tLie/macshot](https://github.com/sw33tLie/macshot), pinned at commit
[`b4d4f3a`](https://github.com/sw33tLie/macshot/tree/b4d4f3a)). It is a single C++20 / Qt 6 application, licensed
GPL-3.0-only. This specification tells implementers what to build: module boundaries and interfaces, rendering
contracts, capture and recording pipelines, data formats, platform backends, security rules, the test strategy, the
build and release pipeline, and the gates that must pass before each milestone.

The product requirements are in [`../prd.md`](../prd.md). The decisions behind this specification, with their
rationale and rejected alternatives, are in
[ADR 0001 — architecture and stack](../adr/0001-architecture-and-stack.md) and
[ADR 0002 — foundation and workflow](../adr/0002-foundation-and-workflow.md). Contributor workflow, coding standards
and review rules are in [`../dev/`](../dev/).

## Files

| File | Covers |
| :--- | :--- |
| [01-architecture-overview.md](01-architecture-overview.md) | Product architecture, processes and threads, component diagram, data flows, invariants, top risks |
| [02-modules-and-interfaces.md](02-modules-and-interfaces.md) | Every CMake module: responsibility, public interface sketch, allowed dependencies and how they are enforced |
| [03-rendering-contracts.md](03-rendering-contracts.md) | The still-image contract (canonical images, fixed inputs, presentation, export) and the studio contract (`FrameScene`, shaders, colour, comparison tests) |
| [04-capture-and-overlay.md](04-capture-and-overlay.md) | Capture backends per platform, the capture ordering contract, the overlay state machine, selection, snapping, keys, finish flows, scroll capture |
| [05-recording-and-studio.md](05-recording-and-studio.md) | Recorder lifecycle, buffer paths, writer states and crash recovery, audio, telemetry, GIF, and the studio editor pipeline and export |
| [06-annotation-model-and-formats.md](06-annotation-model-and-formats.md) | The annotation model, edit state, video project, telemetry and session files: fields, versioning, lenient decoding, limits |
| [07-storage-history-settings.md](07-storage-history-settings.md) | Paths, the settings registry and store, export and import, history revisions, atomic publishing, filenames, cleanup |
| [08-platform-integration.md](08-platform-integration.md) | Capability registry, hotkeys, tray, clipboard, single instance, IPC, CLI, URL scheme, file routes, portals, macOS shims, Desktop Integration page |
| [09-ml-services.md](09-ml-services.md) | OCR, QR, translation, subject masks, faces and people, PII planning, captions, scroll registration, the model catalogue and downloads |
| [10-upload-and-network.md](10-upload-and-network.md) | Uploaders (imgbb, Google Drive, S3-compatible), translation endpoint, update feed, network policy, the offline build |
| [11-security-privacy.md](11-security-privacy.md) | Threat model, untrusted inputs and their bounds, secrets, IPC hardening, logging, fuzzing and hardening, privacy promises |
| [12-testing-strategy.md](12-testing-strategy.md) | Test tiers, the golden-image metric, parity ledger checks, trace and defaults parity, performance and robustness gates |
| [13-build-ci-release.md](13-build-ci-release.md) | Toolchain, presets, dependency policy, CI matrix, versioning, packaging, licensing obligations, ad-hoc macOS signing |
| [14-gates-and-milestones.md](14-gates-and-milestones.md) | Gates G1–G8, milestones M0–M5 with their parity bars, the M1 atomic set, and the initial deviation register |

## How to read this specification

**Normative words.** MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in RFC 2119. Anything not marked with
one of them is explanation.

**Authority order.** When two sources disagree, the higher one wins:

1. MacShot source at `b4d4f3a` for product behaviour, constants, strings, geometry and state machines. Invented
   behaviour is a defect. An intentional difference is legal only with a deviation-register entry (`DEV-nn`).
2. Accepted ADRs for decisions (stack, gates, workflow).
3. This specification for contracts, formats and interfaces.
4. Explanatory text anywhere else.

A contradiction between this specification and an ADR or the MacShot source is a specification defect; fix it in the
same pull request that discovers it.

**Specification lifecycle.** This specification is written before the code. Once a contract has an executable owner
(a header, a schema, a generator, a test, a preset, a script), that owner becomes authoritative and the section here is
reduced to the reasoning plus a pointer to it. Value tables that MacShot already holds in machine-extractable form
(settings keys, single-key shortcuts, hotkey slots, key registries, tool option specs, modifier rules) are **not**
copied here: they are generated into the parity ledger from MacShot source ([12](12-testing-strategy.md)). The
specification names the source location and the rule instead.

**Where parity values are public.** Every parity row is defined publicly by the requirement that covers it in the
PRD ([`../prd.md`](../prd.md), functional requirements and Appendix A) and by the MacShot source it cites. The atomic
ledger lines with their MacShot values live in `parity/`: generated lines from `tools/ledger` (which reads a MacShot
checkout at `b4d4f3a` given as an argument), hand-written lines for rows without a machine-extractable list, and the
deviation register. The harness lane of M0 creates them ([14 §2](14-gates-and-milestones.md#2-m0-gates)). A section
here that defers values to the ledger is complete for implementation only once that row family's ledger files are
merged; a slice that needs them does not start before.

## Conventions

**MacShot citations** are written `macshot/<path>:<line>` and always refer to commit `b4d4f3a`. `<path>` is the path in
the upstream repository, for example `macshot/UI/Overlay/OverlayView.swift:1713`. In code comments, ledger lines and
pull request bodies the full form `macshot/<path>:<line>@b4d4f3a` is used. Contributors who want to open cited lines
locally clone MacShot and point `ARIADSHOT_MACSHOT_DIR` at the checkout.

**Parity identifiers.** The parity ledger ([12](12-testing-strategy.md)) indexes MacShot behaviour by row:

| Prefix | Area | Default domain |
| :--- | :--- | :--- |
| `SH-nn` | application shell and lifecycle | `shot` |
| `OV-nn` | capture overlay | `shot` |
| `TB-nn` | toolbars and tool options | `shot` |
| `AN-nn` | annotation model and tools | `shot` |
| `CR-nn` | capture, scroll capture and recording engine | `rec` |
| `VE-nn` | studio video editor | `studio` |
| `ED-nn` | image editor and secondary windows | `shot` |
| `SV-nn` | services, storage and upload | `shot` |
| `ST-nn` | settings | `shot` |

Rows expand into one ledger line per observable item. Ledger lines may override the default domain; the exceptions are
listed in [14](14-gates-and-milestones.md). `DEV-nn` names a deviation-register entry, `G1`–`G8` a gate and `M0`–`M5` a
milestone. These identifiers belong in documentation, issues, branch names and pull request bodies. They MUST NOT
appear in code comments, test names or commit messages.

**Platform letters** in deviation tables: `A` all platforms, `L` Linux (every desktop), `W` Linux Wayland, `G` GNOME,
`K` KDE Plasma, `M` macOS.

**Evidence markers.**

- **[HYPOTHESIS]** marks an unmeasured number or integration claim. It becomes a fact only after the named gate
  measures it; until then code MUST NOT depend on it being true.
- **[GATE Gn]** marks a choice that gate `Gn` makes. The specification describes every candidate; the gate's ADR
  records the outcome.
- Performance numbers are target thresholds tied to a named workload and topology ([12](12-testing-strategy.md)).

**Units.** MacShot geometry is in points (pt). AriadShot keeps points as the canvas unit: 1 pt equals one logical
pixel at scale 1. Pixel (px) values refer to device pixels. Times are in seconds (s) or milliseconds (ms).

**Interface sketches** are C++ outlines. Class, module and enumerator names are normative. Signatures are indicative:
the first pull request that implements an interface may refine a signature, and it updates this specification in the
same change.

## Glossary

| Term | Meaning |
| :--- | :--- |
| Canonical image | The single `QImage` (ARGB32 premultiplied, sRGB, device pixel ratio equal to the capture scale) that holds a rendered still layer. Every surface displays it; every export encodes it ([03](03-rendering-contracts.md)) |
| Canonical layers | The separate canonical images for committed annotations, the in-progress annotation and the text being edited. Chrome is a separate layer and is never exported |
| Chrome | Toolbars, options rows, popovers, menus, tooltips, badges and handles drawn over a canvas. Never exported |
| View object | A host-agnostic C++ object that ports a MacShot `NSView`: geometry, `paint(QPainter&)`, hit testing, hover, cursor, tooltip, accessibility |
| Surface host | The adapter that maps a platform surface (layer-shell surface, `NSPanel`, override-redirect window, fullscreen toplevel) and forwards events to view objects ([04](04-capture-and-overlay.md)) |
| Capability registry | The run-time record of what the current session can do, resolved from protocol globals, portal versions and probes, never from the desktop name ([08](08-platform-integration.md)) |
| WaylandSession | AriadShot's own second Wayland connection, dispatched on its own thread, carrying capture, data-control, toplevel export, virtual pointer and the Hyprland shortcut fallback |
| Take | One recording: a session folder with the movie, telemetry, optional camera track, status file and project |
| `FrameScene` | The immutable description of one studio output frame, built from a project revision and a composition time; preview and export both render it |
| Revision | One saved state of a history item: image, thumbnail, raw image, annotations and edit state |
| Ledger line | One atomic, independently observable MacShot behaviour with its source, value, domain, milestone and per-platform status |
| Deviation | An intentional, registered difference from MacShot behaviour (`DEV-nn`) |
| Domain | `shot`, `rec`, `studio` or `release`; milestones gate on domains |
| Tier | A test environment class, from offscreen QPA (tier 1) to the live session (tier 4) and macOS (tier M) |
| Reference host | The Hyprland machine on which Linux parity is measured; topology profiles P1–P3 describe its displays ([12](12-testing-strategy.md)) |
