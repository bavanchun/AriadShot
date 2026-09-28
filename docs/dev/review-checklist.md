<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Review checklist

Reviews are adversarial: look for what is wrong, missing or unproven. The reviewer is from a different model family
than the implementer (or a person) and works read-only. Every finding is numbered, actionable and marked **blocking**
or not; the verdict is `PASS`, `PASS_WITH_GAPS` or `FAIL`.

## Correctness

- The code does what the pull request says, including error paths, empty and boundary inputs, and cleanup.
- No undefined behaviour, data races or lifetime errors; ownership follows
  [`coding-standards.md`](coding-standards.md#ownership-and-lifetime).

## Fidelity to MacShot

- Open every MacShot source line the pull request cites, at `b4d4f3a`. Spot-check at least five non-trivial values
  (constants, strings, geometry, defaults, key handling), or all of them when there are fewer.
- Behaviour that MacShot does not have is either registered as a deviation or a defect.
- The ledger lines named in the pull request are the ones the change delivers; status changes carry the evidence their
  class requires.

## Architecture

- Module boundaries hold: `core` QtCore only; `render`, `ui`, `platform` QtGui at most; platform code only in
  `backends/` and `hosts/`; no checks of the desktop's name outside `backends/`.
- Every displayed or exported capture pixel comes from a canonical image; no surface re-renders annotations.
- New dependencies come from system packages or `third_party/` with licence and origin; nothing downloads at build time.

## Tests

- Tests exist for the change, were written from MacShot's values, and assert those values rather than AriadShot's
  constants.
- Negative tests match the checker's own message. No test was weakened, skipped or deleted to pass.
- Goldens changed only in a `golden-update` pull request with before, after and difference images.

## Licensing and provenance

- Every new file has SPDX headers or a `REUSE.toml` entry.
- MacShot-derived material carries the MacShot copyright line and provenance comment, and `PROVENANCE.md` lists it.

## Security and privacy

- Untrusted input is bounded by MacShot's limits and has a fuzz target; no shell interpolation; atomic file writes.
- No secrets, personal data or private paths; nothing logs pixels, clipboard contents, secrets or file contents.
- No new network access beyond user-initiated upload and translation, the update check and consented model downloads.

## Internationalisation and accessibility

- User-visible strings go through `tr()` with sentences as `%1` arguments; no text in images.
- New controls have accessible names and are keyboard-reachable as in MacShot.

## Performance

- The change keeps within the budgets that apply (ADR 0001 §9), or states its measured effect.

## Hygiene

- The pull request title and every commit follow Conventional Commits, without AI references, attribution or plan,
  ledger, gate or milestone identifiers.
- The pull request stays within 500 changed lines or carries the maintainer's `size/exception` label.
- Docs, `AGENTS.md` files and the specification are updated where behaviour, workflow or contracts changed.
