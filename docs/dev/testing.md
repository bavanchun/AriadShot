<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Testing

Day-to-day rules for tests. What the test system must prove, and how, is specified in
[`docs/spec/12-testing-strategy.md`](../spec/12-testing-strategy.md); the executable owner is
[`tests/CMakeLists.txt`](../../tests/CMakeLists.txt).

## Rules

- **Every behaviour change ships with tests.** Write them first, from MacShot's values.
- **Assert MacShot's values, never AriadShot's constants.** A test that compares a default with the constant that
  defines it proves nothing. Cite the value's source as `macshot/<path>:<line>@b4d4f3a`; once the parity ledger exists,
  the ledger line holds the value.
- **Run the narrowest test first** (`ctest --preset dev -R <name>`), then `cmake --workflow --preset dev`, and
  `cmake --workflow --preset asan` before asking for review when the change touches memory, parsing or threads.
- **Names describe behaviour.** Test functions and CTest names never contain ledger, deviation, gate or milestone
  identifiers; the ledger maps identifiers to test names.
- **Never weaken or delete a failing test.** A flaky test is quarantined only with an issue and the maintainer's
  approval. Harness regressions fail: a Wayland test that misses an expected protocol global fails, it never skips.
- **Tests never touch the user's state**: configuration, history, sockets, tray, shortcuts or clipboard. Features that
  own global state honour an instance namespace from their first commit so tests can run them in isolation.

## Declaring a test

```cmake
ariadshot_add_test(NAME render.canonical-image
    SOURCES unit/render/tst_CanonicalImage.cpp
    LIBS AriadShot::render
    LABELS unit)
```

`ariadshot_add_test` builds the Qt Test executable and gives the test the neutral environment below. Script and
build-system tests use `add_test` followed by `ariadshot_test_environment(<name>)`.

| Path | Contents |
| :--- | :--- |
| `tests/unit/<module>/tst_<Topic>.cpp` | unit tests per module |
| `tests/golden/<suite>/` | golden tests and regression goldens `<name>[.<cpu>].png` |
| `tests/trace/` | trace-parity scripts (from M1) |
| `tests/integration/` | headless Sway and nested Hyprland tests |
| `tests/fuzz/` | one libFuzzer target per parser of untrusted input |
| `tests/cmake/` | architecture-check fixtures (standalone CMake projects) |
| `tests/scripts/` | cases for the repository scripts (commit messages, agent guard, include rules, owner gate) |
| `tests/support/` | the shared test library (`ImageCompare`) |
| `tests/sanitizers/` | sanitizer suppressions, each with its reason |

## Labels and tiers

| Label | Meaning | Presets |
| :--- | :--- | :--- |
| `unit` | tier 0 (`QCoreApplication`) and tier 1 (offscreen) tests | every preset except `tsan` |
| `golden` | image comparisons on the offscreen platform | every preset except `tsan` |
| `tsan-safe` | no Qt threading of its own; runs under ThreadSanitizer | `tsan` runs only these |
| `wayland` | tier 2, headless Sway | excluded from `dev` and `asan` |
| `nested-hyprland` | tier 3, nested Hyprland on the reference host | excluded; results attached to the pull request |
| `live` | tier 4, the live session; every run approved by the maintainer | excluded |

The system Qt is not instrumented for ThreadSanitizer, so `tsan` runs only `tsan-safe` tests, with suppressions for
QtTest's own watchdog thread ([`tests/sanitizers/tsan.supp`](../../tests/sanitizers/tsan.supp)). Suppressions name
single functions and are added only with a reproduced report.

## The neutral environment

Every test runs with `QT_QPA_PLATFORM=offscreen`; with `QT_QPA_PLATFORMTHEME`, `QT_STYLE_OVERRIDE`, `QT_IM_MODULE`,
`QT_PLUGIN_PATH`, `QT_SCALE_FACTOR`, `QT_SCREEN_SCALE_FACTORS`, `QT_AUTO_SCREEN_SCALE_FACTOR`,
`QT_ENABLE_HIGHDPI_SCALING`, `QT_FONT_DPI`, `DBUS_SESSION_BUS_ADDRESS` and `DEBUGINFOD_URLS` unset; and with its
own `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR` (mode 0700) and
`ARIADSHOT_TEST_OUTPUT_DIR` under `build/<preset>/tests/environment/<test>/`, recreated at the start of every CTest
run. Tests that use `QStandardPaths` also call `QStandardPaths::setTestModeEnabled(true)`.

The reason: a desktop platform theme such as `gtk3` loads fontconfig, Pango and GLib into the test process, which makes
goldens host-dependent and produces leak reports under AddressSanitizer. The session bus is hidden too, because Qt
reaches the desktop's tray over D-Bus even on the offscreen platform; libdbus falls back to `$XDG_RUNTIME_DIR/bus`, so
both the unset `DBUS_SESSION_BUS_ADDRESS` and the private `XDG_RUNTIME_DIR` are needed. `DEBUGINFOD_URLS` is unset
so that a sanitizer report is never symbolized with debug information downloaded during the test: tests fetch
nothing, and a report reads the same on a workstation as in CI. The sanitizer presets pass with such a theme set in
the calling shell. Packaging `check()` steps unset the same variables. On Linux, `render::FontSet` explicitly registers
the bundled fonts from compiled resources. The offscreen Qt platform ignores `QT_QPA_FONTDIR`, so tests do not use that
environment variable for font selection.

Under ThreadSanitizer (`tsan`), tests run with `ignore_noninstrumented_modules=1`: the system Qt and libstdc++ are
not instrumented, so only races that AriadShot's instrumented code takes part in are reported. The suppressions in
`tests/sanitizers/tsan.supp` name exported symbols only.

## Tier 2: Headless Sway (`wayland`)

Tier-2 tests run against a script-started headless Sway instance (`tests/integration/harness/run-headless-sway.sh`)
with two outputs (one rotated 90°). The CTest fixture `wayland_sway` automatically starts and stops the harness.

```bash
ctest --test-dir build/dev -L '^wayland$' --output-on-failure
# Or wrap directly: tests/integration/harness/run-headless-sway.sh <command>
```

## Golden images

The comparator is [`tests/support/ImageCompare`](../../tests/support/ImageCompare.h), tested against the published
CIEDE2000 data of Sharma, Wu and Dalal (2005). Golden reviews can use
[`ariadshot-imgdiff`](../../tools/imgdiff/) with `--class exact|presentation|corpus [--mask mask.png] actual.png
expected.png [--out dir]`.

- **Exact class** — canonical renders, PNG round trips, same-platform regression goldens, presentation at integer
  scale: equal size and device pixel ratio, and byte-equal pixels after converting both images to
  `QImage::Format_RGBA8888`.
- **Perceptual class** — per pixel, on un-premultiplied colours: both alphas below 8/255 pass; an alpha difference
  above 1/255 fails; otherwise the pixel passes when the CIEDE2000 difference is at most T. The image passes when at
  least P of the pixels outside the declared tolerance mask pass. Fractional-scale presentation: T = 1, P = 99.9 %.
  AriadShot against the MacShot corpus: T = 2, P = 99.5 %; Linux masks cover only font raster, emoji glyphs and
  effect-preset looks, and each mask names its deviation entry.
- **Failures** write `actual.png`, `expected.png`, `mask.png` and `statistics.txt` into the test's output directory.
  The test fails through `QVERIFY` in the test function itself.
- **Custody.** Run a test with `ARIADSHOT_WRITE_GOLDEN_CANDIDATES=1` to write candidates into its output directory
  (`build/<preset>/tests/environment/<test>/output/candidates/`). Candidates reach `tests/golden/` only in a
  human-reviewed pull request labelled `golden-update`, with before, after and difference images, merged after the
  maintainer's approval. Agents never create or update golden images to make a test pass. MacShot corpus goldens come
  only from the MacShot build on the maintainer's Mac and live in a separate corpus repository.
- A golden may have a per-architecture variant (`scene.arm64.png`) when rasterisation legitimately differs between
  x86-64 and arm64; the comparator prefers it when it exists.

## Tests of the build system and scripts

Negative cases must fail for the right reason: each is a CTest test whose `PASS_REGULAR_EXPRESSION` matches the
checker's own message, so an unrelated error cannot make it pass.

- `build.arch-*` configure the fixtures in `tests/cmake/` with the real architecture check, including Qt's
  plugin-import expression built by Qt's own helper (allowed, forbidden, and around a target that is not a plugin).
- `scripts.arch-include.*` run `scripts/check-architecture.sh` on the trees in `tests/scripts/arch-include-cases/`.
- `scripts.commit-message.*` run one case each of `tests/scripts/commit-message-cases.txt`.
- `scripts.agent-guard.<runtime>.*` feed Claude- and agy-shaped payloads from `tests/scripts/agent-guard-cases.tsv`
  through the real adapters, in throwaway repositories.
- `scripts.gate-inspect.*` feed recorded pull request payloads to the owner gate's decision script.
- `scripts.check-repo.*` run `scripts/check-repo.sh` in throwaway repositories: a private path that is staged but
  hidden by a different working copy, private paths in a commit, files under private directories (staged, and
  committed in a nested directory), a clean tree, and an untracked `CLAUDE.md` (`tests/scripts/run-check-repo-case.sh`).

Add a case to the case file instead of writing a new test when a rule changes.
