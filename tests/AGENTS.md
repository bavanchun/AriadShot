<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# tests — local rules

Full rules: `docs/dev/testing.md`. Strategy: `docs/spec/12-testing-strategy.md`.

- **Declare tests with `ariadshot_add_test(NAME <area>.<topic> SOURCES … LIBS … LABELS …)`** in `tests/CMakeLists.txt`.
  It gives every test the neutral environment: offscreen platform, the host's Qt variables unset, no session bus, no
  debug-information downloads, and a fresh home and XDG directory set under the build directory. Tests never touch
  the user's configuration, history, sockets, tray, shortcuts or clipboard.
- **Names describe behaviour**, never ledger, deviation, gate or milestone IDs. File names: `tst_<Topic>.cpp`.
- **Labels:** `unit`, `golden`, `wayland` (headless Sway), `nested-hyprland`, `live` (owner-approved runs),
  `gpu` (hardware GPU or VA-API) and `tsan-safe` (no Qt threading). The `dev`, `asan` and `release` presets exclude
  `wayland`, `nested-hyprland`, `live` and `gpu`.
- **Assert MacShot values** from the parity ledger with their `macshot/<path>:<line>@b4d4f3a` source, never the
  AriadShot constant under test.
- **Goldens:** compare through `tests/support/ImageCompare`. Never create or update a golden image to make a test pass;
  candidates come from `ARIADSHOT_WRITE_GOLDEN_CANDIDATES=1` and reach `tests/golden/` only in a reviewed
  `golden-update` pull request.
- **Negative tests pass for the right reason:** match the checker's own message with `PASS_REGULAR_EXPRESSION`.
- **Never weaken or delete a failing test.** A flaky test is quarantined only with an issue and the owner's approval.
- **Fixtures stay small** and carry SPDX headers; `tests/scripts/` holds the cases for repository scripts.
