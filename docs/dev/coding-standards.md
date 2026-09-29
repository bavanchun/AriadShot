<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Coding standards

These rules apply to every change. Formatting and most naming rules are mechanical (`.clang-format`, `.clang-tidy`);
this page gives the rest and the reasons. Module-specific rules are in the nested `AGENTS.md` files listed in the root
[`AGENTS.md`](../../AGENTS.md).

## Invariants

1. **Fidelity is the product.** Behaviour, constants, strings, geometry and state machines come from MacShot's source
   at `b4d4f3a`. Invented behaviour is a defect. An intentional difference is legal only with a deviation-register
   entry, and the code or test that implements it says which behaviour it follows.
2. **Module boundaries.** `core` uses QtCore only; `render`, `ui` and `platform` use QtGui at most, never QtWidgets or
   Qt Quick; platform headers appear only in `src/backends/` and `src/hosts/`. Code asks the capability registry what
   the session can do and never checks the desktop's name. The configure-time link check and
   `scripts/check-architecture.sh` enforce this; see [`docs/spec/02-modules-and-interfaces.md`](../spec/02-modules-and-interfaces.md).
3. **One pixel source.** Every exported or displayed capture pixel comes from a canonical image made by `render/`
   ([`docs/spec/03-rendering-contracts.md`](../spec/03-rendering-contracts.md)). Surfaces display canonical images and
   never re-render annotations.
4. **Two Wayland connections with fixed ownership**: Qt's for surfaces and input, AriadShot's own for capture,
   data-control, toplevel export and synthetic input, on its own thread. The daemon holds no screenshot pixels while
   idle.
5. **Privacy.** No telemetry. Network access only for user-initiated upload and translation, the update check and
   consented model downloads.

## Language

- C++20 without compiler extensions. A library feature that fails on any required CI job is not used.
- RAII everywhere; `const` by default; `[[nodiscard]]` on functions whose result must be checked.
- No exceptions across module boundaries or through the event loop. Fallible operations return
  `ariadshot::Expected<T, E>` (vendored `tl::expected`, added with the first fallible API) with a module error enum and
  a `describe()` for logs. Qt's own conventions stay where Qt defines them (a null `QImage` for "no image").
- User-facing failures use MacShot's surfaces (error pill, dialog, toast) and are never silent.

## Ownership and lifetime

- `QObject` trees own their children through parents. Non-`QObject` ownership uses `std::unique_ptr`; `std::shared_ptr`
  only for real shared ownership. Raw pointers and references never own. No naked `delete`.
- `QPointer` for `QObject`s observed across asynchronous boundaries.
- Declare members in the order their destruction must happen (last declared is destroyed first).

## Threading

- GUI objects live on the GUI thread only. Pixel work runs in worker objects moved to a `QThread` or as pure functions
  on `QtConcurrent` with cancellation.
- The Wayland session thread uses no Qt GUI classes.
- Threads communicate through queued signals or lock-protected queues; the GUI thread never blocks on a wait.
- Each class header states its thread affinity: `Thread: GUI`, `Thread: WaylandSession` or `Thread: any`.

## Qt idioms

- Functor-based `connect` only.
- `QT_NO_KEYWORDS`: write `Q_EMIT`, `Q_SIGNALS` and `Q_SLOTS`.
- `QT_NO_CAST_FROM_ASCII` and `QT_NO_CAST_TO_ASCII`: every string is explicit — `tr()` for user-visible text,
  `QStringLiteral` or `u"…"_s` otherwise.
- `QProcess` with an argument list, never a shell command string.
- Deprecated API up to Qt 6.8 is disabled (`QT_DISABLE_DEPRECATED_UP_TO=0x060800`).

## Internationalisation and accessibility

- Every user-visible string goes through `tr()`, with a disambiguation comment when the string is short. Sentences use
  `%1` arguments, never concatenation (MacShot's catalogues use positional arguments). No text is baked into images.
  Layouts mirror for right-to-left languages.
- Strings come from MacShot's catalogues; where the product name appears, AriadShot's name replaces MacShot's.
- Every chrome view object implements `QAccessibleInterface`. Keyboard reachability follows MacShot. Standard widgets
  keep accessible names.

## Logging

- One `Q_LOGGING_CATEGORY` per module, named `ariadshot.<module>` (`ariadshot.app`, `ariadshot.backends.wayland`),
  declared in the module's `Logging.h`.
- Never log pixels, clipboard contents, secrets, tokens or file contents. Paths only at debug level: they contain user
  names.

## Ported constants and MacShot material

- A constant ported from MacShot is a named `constexpr` value (`kPascalCase`) in one header per subsystem, commented
  with the behaviour it controls and its source as `macshot/<path>:<line>@b4d4f3a`. Defaults come only from the
  settings registry.
- A file containing MacShot-derived material (strings, tables, ported algorithms, constants) carries
  `SPDX-FileCopyrightText: sw33tLie and MacShot contributors` next to the AriadShot line and a provenance comment, and
  gets a row in [`PROVENANCE.md`](../../PROVENANCE.md) in the same change.
- Comments explain behaviour and cite MacShot; they never carry ledger, deviation, gate or milestone identifiers.

## Files and names

| Item | Convention |
| :--- | :--- |
| C++ files | `PascalCase.h`, `.cpp`, `.mm`; one primary class per file, named after it (`OverlayCanvas.cpp`), which keeps the mapping to MacShot's type-named Swift files obvious |
| Includes | include root `src/`: `#include "core/BuildInfo.h"`; order own header, project, Qt, standard and system (clang-format regroups them) |
| Headers in targets | listed as private sources of the owning target, so AUTOMOC runs once |
| Classes, structs, enums, enumerators | `PascalCase` |
| Functions, methods, variables, parameters | `camelCase` |
| Private and protected members | `m_camelCase` |
| Static members | `s_camelCase` |
| Constants and `constexpr` values | `kPascalCase` |
| Namespaces | `ariadshot::<module>`, `ariadshot::backends::<platform>` |
| Macros | `UPPER_CASE`, and only when unavoidable |
| Tests | `tests/unit/<module>/tst_<Topic>.cpp`; CTest name `<module>.<topic>` |
| Scripts, documents, workflows | kebab-case (`check-architecture.sh`, `coding-standards.md`) |
| CMake modules | `cmake/AriadShot<Topic>.cmake` |
| Branches | `<type>/<kebab-topic>`; may carry a ledger or gate token (`feat/sh-08-status-menu`) |

## Formatting and static analysis

- clang-format 22 with [`.clang-format`](../../.clang-format): LLVM base, 4-space indent, 120 columns, left pointer
  alignment, attached braces. Run `clang-format -i` on changed files; `scripts/check-format.sh` checks.
- clang-tidy with [`.clang-tidy`](../../.clang-tidy) (warnings are errors) and clazy levels 0 and 1 run in CI on
  tracked sources. Fix findings; suppress one only with a `NOLINT(<check>)` comment that states why the code is right.
- Compiler warnings are errors in CI. Do not disable a warning to get a build green.

## Security

- Parsers of untrusted input (clipboard HTML and RTF, images, settings imports, history, project and telemetry files)
  are bounded by MacShot's own limits and get a fuzz target before they merge.
- Raw pixel loops live only in `render/pixel/`, behind bounds-checked `std::span` accessors.
- Files are written atomically (write, sync, rename). The control socket lives in a 0700 runtime directory. URL-scheme
  and CLI input is validated. No shell interpolation. Secrets go through QtKeychain, with a visible degraded state when
  no keyring exists. Dependencies come only from system packages or `third_party/`.
