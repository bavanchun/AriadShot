<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# AriadShot — instructions for coding agents

AriadShot is a faithful Linux and macOS remake of MacShot (upstream https://github.com/sw33tLie/macshot at commit
`b4d4f3a`), written in C++20 with Qt 6 and licensed GPL-3.0-only. Product requirements: `docs/prd.md`. Specification:
`docs/spec/`. Decisions: `docs/adr/`. Local agents read MacShot source through `$ARIADSHOT_MACSHOT_DIR`.

## Commands (verified by the scaffold)

- `cmake --workflow --preset dev` · `ctest --preset dev -R <name>` · `cmake --workflow --preset asan`
- `scripts/check-all.sh` (every check CI runs) · manager only: `scripts/worktree.sh create|shell|remove|list`
- Start every agent session at the worktree root: Claude Code loads the agent guard (`.claude/settings.json`) only
  there, and agy reads `.agents/hooks.json` from there.

## Before editing, read the nested file for that directory

- `src/core/AGENTS.md`
- `src/render/AGENTS.md`
- `src/platform/AGENTS.md`
- `src/ui/AGENTS.md`
- `src/media/AGENTS.md`
- `tests/AGENTS.md`

Keep this list complete: `scripts/check-repo.sh` fails when a nested `AGENTS.md` is missing from it. Codex and agy load
nested files only through this list.

## Invariants (full list: `docs/dev/coding-standards.md`)

1. **Fidelity.** Behaviour, constants, strings and geometry come from MacShot source at `b4d4f3a`; invented behaviour
   is a defect. An intentional difference needs a deviation-register entry (`docs/spec/14-gates-and-milestones.md` §5,
   later `parity/deviations.jsonl`). Cite MacShot as `macshot/<path>:<line>@b4d4f3a`.
2. **Module boundaries.** `core` uses QtCore only; `render` and `ui` use QtGui only; platform headers only in
   `src/backends/` and `src/hosts/`. Ask the capability registry, never the desktop name. The configure step and
   `scripts/check-architecture.sh` enforce this.
3. **One pixel source.** Every exported or displayed capture pixel comes from a canonical image made by `render/`.
4. **Two Wayland connections** with fixed ownership (Qt's for surfaces and input, AriadShot's own for capture,
   clipboard and synthetic input); the daemon holds no screenshot pixels while idle.
5. **Privacy.** No telemetry. Network only for user-initiated upload and translation, the update check and consented
   model downloads.

## Forbidden

- Pushing to `main`; force-pushing except `--force-with-lease` on your own branch; `--no-verify`.
- Editing outside your packet's write set, in the main checkout, or under `$ARIADSHOT_MACSHOT_DIR`.
- Merging pull requests, pushing tags, creating releases, approving the owner gate, or changing repository settings,
  rulesets, environments or secrets: the owner does these, even when a command would succeed.
- Pushing changes to `.github/workflows/`: prepare them; the owner pushes them.
- Using the owner's credentials (`~/.config/gh`, SSH keys, keyrings) or browser session. Use the agent token of your
  pane.
- `sudo` or package installs; changing the live desktop session; live capture or recording without the owner's
  approval.
- Creating or updating golden images, or marking ledger lines verified, without the evidence the rules require.
- References to AI models, agents or tools, or plan, ledger, gate or milestone IDs (`SH-08`, `DEV-13`, `G1`, `M1`) in
  commit messages, code comments or test names. AI attribution in commits or pull request bodies.
- Creating `CLAUDE.md`, `CLAUDE.local.md`, `.claude/CLAUDE.md` or `GEMINI.md`, including through a docs or
  folder-context skill: any of them switches Claude Code off `AGENTS.md`.
- Secrets or personal data anywhere in the repository.

## Workflow

- One task = one packet + one worktree + one branch + one pull request. Tests first, from the MacShot values.
- Commit messages: Conventional Commits, `<type>(<scope>): <subject>`, lower-case subject, no final period, at most
  100 characters. Types and scopes: `CONTRIBUTING.md`. Never add `Signed-off-by`; only a human can certify the DCO.
- Stop every process you started. End every run with the status block:
  `Status: DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT`, `Summary:`, `Concerns/Blockers:`.
- Escalate hard design problems to the manager instead of guessing; report uncertainty as `DONE_WITH_CONCERNS`.
- Details: `docs/dev/agent-workflow.md`. Review rules: `docs/dev/review-checklist.md`.

## Definition of done

- `cmake --workflow --preset dev` and `cmake --workflow --preset asan` pass; `scripts/check-all.sh` is green.
- Tests assert MacShot values from the ledger, never AriadShot constants.
- Docs, the ledger and `PROVENANCE.md` are updated where the change touches them.
