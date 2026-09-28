<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# ADR 0002: Foundation and workflow

- **Status:** accepted, 2026-09-28 (third revision, with the maintainer's final decisions).
- **Decided by:** the maintainer, on the lead architect's foundation record after two adversarial reviews.
- **Scope:** how AriadShot is built: execution method, repository and module layout, toolchain and dependency policy,
  headless tests and the golden-image metric, the git and GitHub workflow, how people and coding agents work together,
  where rules live and how they are enforced, and the scaffold.

This ADR is the public summary of the maintainer's foundation decision record, which is kept privately with the
research, host probes and runtime experiments behind it. [ADR 0001](0001-architecture-and-stack.md) is binding for it:
nothing here reopens the stack. Operational detail lives in [`docs/dev/`](../README.md#contributor-guides) and in the
scripts, presets and workflows themselves.

## Context

- One maintainer owns the project. Most code is written by several AI coding agents from different vendors, working in
  parallel under a manager agent, with reviews by an agent from a different model family. The maintainer approves every
  outward-facing action.
- The repository is public and GPL-3.0-only; it holds no secrets, no personal data and no private paths.
- MacShot at `b4d4f3a` is the authority for behaviour. Parity is measured line by line against it.
- The reference host is Arch Linux (rolling) with Qt 6.11 and Hyprland; the maintainer has a Mac for macOS testing.

## Decisions

### 1. Execution: gates first, then ledger-driven vertical slices

- **M0 runs the eight gates of ADR 0001 in four lanes**: a harness lane first (the golden metric, parity-ledger
  tooling, the still-rendering gate G3), then Wayland (G1, G2, G4, developed on a nested Hyprland and measured finally
  on the live session with the second display connected), media (G5, G6) and macOS (G7 and the MacShot reference corpus
  on the maintainer's Mac). G8 closes M0. Each gate outcome becomes an ADR.
- **From M1, work is a sequence of vertical slices**, each a named set of parity-ledger lines delivered end to end with
  the tests that prove them, starting with a walking skeleton (hotkey or CLI → capture → overlay → select → copy).
  Inside a slice work follows the module graph: model and algorithms, rendering, view objects, host and window wiring,
  platform backends. Tests are written first from the ledger's MacShot values.
- **Gate code policy:** a gate spike that proves a design becomes its module's first real code, reviewed like any pull
  request; throwaway experiment code is not merged.
- **The parity ledger** is JSON Lines under `parity/`: generated lines (from MacShot source, by a generator that takes
  the MacShot checkout as an argument), hand-written lines, per-platform status files kept separate from generated
  files, and the deviation register. Statuses: `open`, `verified-source`, `verified`, `deviation`, `not-applicable`. A
  line becomes `verified` only with the evidence its class requires; see
  [`docs/spec/12-testing-strategy.md`](../spec/12-testing-strategy.md) §5.
- **Citing MacShot:** `macshot/<path>:<line>@b4d4f3a` in ledger lines, pull request bodies and provenance comments;
  never a local path. Ledger, deviation, gate and milestone identifiers stay out of code, test names and commit messages.

### 2. Repository boundary and layout

- **This repository is the public product.** The maintainer's workspace around it is private: plans and reports, the
  MacShot reference checkout (read-only at the file-system level, which protects it from every tool at once), and a
  sibling directory of task worktrees. Local agents receive exact paths in their task packet and through four
  environment variables: `ARIADSHOT_WORKSPACE`, `ARIADSHOT_MACSHOT_DIR`, `ARIADSHOT_PLANS_DIR`,
  `ARIADSHOT_WORKTREES_DIR`. The repository names MacShot only by its upstream URL and commit; `scripts/check-repo.sh`
  rejects private paths.
- **Layout:** `src/<module>/` per [`docs/spec/02-modules-and-interfaces.md`](../spec/02-modules-and-interfaces.md),
  `tests/`, `cmake/`, `scripts/`, `packaging/`, `docs/` (PRD, specification, ADRs, contributor guides), and later
  `parity/`, `resources/`, `third_party/`, `tools/` and `translations/` when the work that needs them arrives.
- **One CMake target per module**, `ariadshot_<module>` with the alias `AriadShot::<module>`, declared with its allowed
  dependencies. A configure-time check enforces direct edges for every module and reachability for `core`, `render`
  and `ui` (allowed paths such as `render → core → Qt6::Core` pass; aliases, `$<LINK_ONLY:…>` and `$<BUILD_INTERFACE:…>`
  are normalised; anything else fails closed). `scripts/check-architecture.sh` checks `#include` lines per directory,
  including headers pulled in through an allowed module, and rejects desktop-name checks outside `src/backends/`.
  Fixtures prove both checkers fail with their own messages. Modules without code are INTERFACE targets: they declare
  the graph and carry no placeholder code.
- **Naming:** C++ files in PascalCase named after their primary class (`OverlayCanvas.cpp`), mirroring MacShot's
  type-named Swift files; tests `tst_<Topic>.cpp` with CTest names `<module>.<topic>`; scripts, docs and workflows in
  kebab-case; classes and enums PascalCase, functions and variables camelCase, `m_` members, `s_` statics,
  `kPascalCase` constants, namespaces `ariadshot::<module>`; the application ID `io.github.bavanchun.AriadShot`;
  executables `ariadshot-daemon` and, later, a Qt-free `ariadshot` CLI.

### 3. Toolchain and dependencies

- **Compilers:** GCC (the host default) and Clang on Linux, Apple Clang on macOS; C++20 without extensions; CI decides
  which library features are usable. Fallible APIs use `ariadshot::Expected<T, E>` (vendored `tl::expected`, because
  `std::expected` is C++23).
- **CMake 3.28 or newer with presets**: `dev`, `dev-clang`, `asan`, `tsan`, `release`, and `ci-dev`, `ci-asan`,
  `ci-tsan`, which add warnings-as-errors (`cmake --workflow` accepts no `-D`). Every CI build job runs exactly one
  `cmake --workflow --preset ci-<x>` command, which reproduces the job locally. Qt Core, Gui, Widgets (and Test) are the
  only mandatory components; every other dependency is found by the module that needs it behind an option that
  defaults to off and is required when on. No auto-detected launchers or linkers.
- **Version:** `project(VERSION)` is the source of truth; the build writes `PROJECT_VERSION` on the release tag and
  `PROJECT_VERSION+g<short hash>` otherwise.
- **Dependencies, two phases.** Acquisition (packages, a pinned Qt for CI, the pinned clang-format, the corpus) may use
  the network; configure, build and test fetch nothing. `FetchContent`, `ExternalProject` and `file(DOWNLOAD)` are
  banned, which `scripts/check-no-network-build.sh` enforces as a policy check. Dependencies come from system packages
  or are vendored in `third_party/` with their licence and origin. CI pins Qt 6.8.4 on Ubuntu and macOS and checks the
  version found; the Arch jobs roll with the distribution. The scaffold needs no packages beyond a compiler, CMake,
  Ninja and Qt.
- **Warnings, definitions, hardening, sanitizers:** strict warnings on every target; `QT_NO_KEYWORDS`, the
  `QT_NO_CAST_*` family and `QT_DISABLE_DEPRECATED_UP_TO=0x060800`; `_FORTIFY_SOURCE=3`, `_GLIBCXX_ASSERTIONS`, stack
  protection, CET, PIE and full RELRO in release builds on Linux; ASan and UBSan on the whole offscreen suite, TSan on
  tests labelled `tsan-safe`.
- **Lint and format:** clang-format 22.1.8 everywhere; curated clang-tidy and clazy on tracked sources only (so
  generated MOC output is never analysed); shellcheck, actionlint, REUSE and gitleaks. Plain git hooks call the same
  scripts CI calls.
- **Headless tests in tiers:** offscreen QPA with the host's Qt environment neutralised; headless Sway in CI for wlroots
  protocol clients; nested Hyprland on the reference host for Hyprland-specific protocols and gates G1/G2; live-session
  runs approved by the maintainer; hosted macOS runners plus the maintainer's Mac. A tier-2 or tier-3 test that misses
  a protocol global fails; it never skips.
- **Golden-image metric:** an *exact* class (byte equality of un-premultiplied RGBA8888 pixels plus size and device
  pixel ratio) and a *perceptual* class (per-pixel CIEDE2000 on un-premultiplied sRGB, with alpha rules), with
  thresholds T = 1, P = 99.9 % for fractional-scale presentation and T = 2, P = 99.5 % against the MacShot corpus. The
  comparator is tested against the published CIEDE2000 data. Agents never create or update golden images to make a test
  pass. The MacShot corpus lives in a separate repository, fetched at a pinned commit.
- **CI:** jobs `lint`, `linux-gcc`, `linux-clang`, `linux-qt-floor`, `macos` (`macos-26`) and `macos-14` (until GitHub
  retires that image on 2026-11-02; a macOS 14 VM runner on the maintainer's Mac then runs on a schedule), plus the
  `commit-policy` job. Actions are pinned by commit, workflows get read-only tokens and no secrets, and caches are
  treated as untrusted input.

### 4. Git and GitHub

- **Trunk-based** development with short-lived topic branches in per-task worktrees; squash-only merges whose commit
  is the pull request title (with its number) and an empty body.
- **Commits:** Conventional Commits, checked by `scripts/check-commit-message.sh` in the `commit-msg` hook and in CI,
  which also rejects AI references and attribution and plan, ledger, gate and milestone identifiers. Pull requests
  change at most 500 lines unless the maintainer applies `size/exception`.
- **Identity** (maintainer's decision): no machine account and no GitHub App; agents act through the maintainer's
  account with a **fine-grained token** limited to this repository, expiring within 90 days, without Administration,
  Deployments, Commit statuses, Workflows, Environments, Secrets or Variables permissions.
- **The owner gate.** The maintainer cannot approve their own pull requests, so rulesets require zero approvals and the
  maintainer's consent comes from a required check, `owner-consent`, created on the pull request's head commit by a
  `pull_request_target` workflow that waits on the `owner-review` environment, which only the maintainer can approve.
  The agent token cannot approve it, fake it, rerun it or edit it. Required checks are strict, so a head behind `main`
  cannot merge and updating it needs a new approval. A fork pull request that changes workflows fails the gate. The
  gate uses `pull_request_target` the way GitHub documents as safe: it checks out and runs none of the pull request's
  code, and its token only reads pull requests and writes check runs. Because GitHub blocks that event in public
  repositories by default from 2026-11-02, a repository Actions event policy permits `pull_request_target` for the
  gate's workflow file only; the maintainer's setup script creates it and the verification script checks it.
- **Trusted policy checks.** The `lint` and `commit-policy` checks run on `pull_request` without secrets, check out
  the pull request as data next to its base commit, and run the base commit's checkers against it, so a pull request
  cannot weaken the checks that judge it. Only the pull request that adds the checkers to the repository's first commit
  is judged by its own, and its job summary says so. Workflow files are protected by who can change them instead:
  agents cannot push them, and fork changes to them fail the gate. The details are in
  [`docs/dev/agent-workflow.md`](../dev/agent-workflow.md#trusted-policy-checks).
- **Rulesets without bypass actors:** on `main`, deletion and force pushes are blocked, history is linear, a pull request
  with resolved conversations and squash-only merging is required, and so are the CI checks and `owner-consent`, each
  pinned to the GitHub Actions app. On `refs/tags/v*`, creation, update and deletion are restricted; the maintainer's
  release script adds a temporary bypass for the release tag and removes it again. There is no signed-commit rule.
- **What GitHub cannot enforce with one account** — an agent merging a pull request after the maintainer approved its
  gate, publishing a release on an existing tag, or using the maintainer's other credentials on the same machine — is
  covered by the agent guard, the rules and the process, and is stated as such in
  [`docs/dev/agent-workflow.md`](../dev/agent-workflow.md).
- **Releases:** SemVer 0.x by milestone (`v0.1.0` at M1 … `v1.0.0` at M5), a changelog generated by git-cliff from the
  squash titles, and SSH-signed annotated tags created by the maintainer.
- **macOS distribution without a Developer ID** (maintainer's decision, DEV-40): ad-hoc inside-out signing verified with
  `codesign --verify --deep --strict`, never notarized; the documented Gatekeeper first-open steps; no official Homebrew
  cask; Sparkle with EdDSA signatures only, the key held by the maintainer. Whether privacy grants survive updates is
  measured in G7, and a self-signed project certificate remains the maintainer's option.
- **Secrets:** gitleaks in the pre-commit and pre-push hooks and on every CI range, GitHub secret scanning with push
  protection, ignore rules for keys and environment files, and no path allowlists.
- **DCO 1.1** for external contributors, with the sign-offs copied into the squash commit body at merge; the
  maintainer's account and Dependabot are exempt; agents never sign off. SPDX headers through REUSE on every file;
  **GPL-3.0-only for every file**, so no MacShot-derived code can be labelled "or later"; MacShot material is listed in
  `PROVENANCE.md`.
- **Tracking:** GitHub milestones M0–M5, labels by type, area, domain, platform, gate and status, and issue forms for
  gates, parity slices, parity defects and bugs. The ledger in the repository is the truth for parity.

### 5. How people and agents work

- **Roles:** the maintainer (approvals, merging, releases, settings, live and Mac testing); a manager agent (plans, task
  packets, worktrees, integration, git operations for sandboxed agents); implementers; reviewers from a different model
  family, working read-only and printing their report.
- **The loop:** issue or plan phase → task packet → worktree → tests first → local checks → push with the agent token →
  pull request → CI → cross-family review → fixes → maintainer review and gate approval → squash merge by the
  maintainer → ledger update → worktree removal with its processes stopped.
- **One task = one packet = one worktree = one branch = one pull request.** Parallel tasks have disjoint directory
  write sets; shared files (module `CMakeLists.txt`, `cmake/`, `scripts/`, `.github/`, `AGENTS.md`) have one owner per
  task. Every agent run ends with `Status: DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT`.
- **Agent panes start at the worktree root in a clean environment** (`scripts/worktree.sh shell`): no shell profile,
  aliases or functions, only the agent token, so no wrapper can silently remove a sandbox.
- **The maintainer's approval** is needed for merging, workflow changes, tags and releases, settings, host packages,
  changes to the live desktop session, live capture and recording tests, and anything a packet does not cover. A
  standing authorization lets agents build and test locally, run headless compositors started by repository scripts,
  run a non-capturing smoke test on the live session, push their own topic branches and open their own pull requests.
- **macOS verification is routine:** required hosted macOS jobs on every pull request, and `scripts/macos/verify.sh`
  plus [`docs/dev/macos-checklist.md`](../dev/macos-checklist.md) on the maintainer's Mac for slices that touch
  rendering, hosts, windows, the macOS backend or packaging.

### 6. Rules and enforcement

- **One instruction file, `AGENTS.md`,** read by every agent runtime, plus a few nested `AGENTS.md` files that the root
  file lists by path. The list is load-bearing: only some runtimes load nested files by themselves, so
  `scripts/check-repo.sh` keeps it complete. There is no `CLAUDE.md` or `GEMINI.md`: a `CLAUDE.md` anywhere up the tree
  switches Claude Code to CLAUDE.md-only mode and stops `AGENTS.md` from loading, so `check-repo.sh` rejects such files,
  tracked or not, in the pre-commit hook and in CI. Every rule that must bind all runtimes (commit rules, identifiers,
  status protocol, process hygiene, no attribution) is written into the repository's `AGENTS.md`, never left to a
  runtime's global file.
- **Detail lives in `docs/dev/`** (coding standards, testing, agent workflow, review checklist, building).
- **Mechanical layers:** a shared agent guard (`scripts/agent-guard/`, one policy with adapters for Claude Code and
  agy) that blocks pushes to `main`, force pushes, tag pushes, destructive git commands, deleting the repository or the
  MacShot tree, writes into `.git/` or the MacShot tree, and GitHub actions reserved to the maintainer; git hooks;
  CI checks; and the server-side rulesets. The guard is a text-level safety net for accidents, not a security boundary;
  the rulesets, the token's missing permissions and the read-only MacShot tree are the controls that hold.

### 7. Scaffold

The first pull request contains: the CMake project, presets, the module graph with its checks and negative fixtures; a
tray-only `ariadshot-daemon` (a thin `main.cpp` over an `ariadshot_app` library with the tray controller and a
SIGINT/SIGTERM bridge, `--help` and `--version`); `core` (build information) and `render` (the canonical image factory)
as the only other code modules; unit, golden, comparator and checker tests; lint and format configuration; the
scripts, hooks and agent guard; CI, PR-policy and owner-gate workflows; the GitHub setup, identity and release scripts;
community files; the PRD, specification, these ADRs and the contributor guides; and packaging skeletons (an AUR `-git`
recipe and a desktop entry).

## Consequences

- No pull request merges before the maintainer approved its exact head commit containing the current `main`; the
  residual risk after approval is timing, not content. When `main` moves, open pull requests need an update and a
  second approval.
- Workflow changes can only be pushed by the maintainer, because the agent token has no Workflows permission.
- A change to a policy checker is judged by the checker it replaces and applies from the next pull request on.
- The owner gate depends on the event policy. If GitHub does not allow one for this repository, the gate cannot run
  after 2026-11-02; the maintainer then merges every pull request personally until the gate is redesigned.
- Blank squash bodies keep commit history free of plan identifiers and checklists; the detail stays in the pull request,
  linked from every commit title.
- The single-file instruction topology depends on how each agent runtime loads files; the loading behaviour is
  re-checked with a canary whenever a runtime is upgraded.
- Un-notarized macOS releases add a one-time Gatekeeper step for users; the project never teaches users to disable
  Gatekeeper or strip quarantine attributes.

## Evidence and history

The private foundation record has three revisions: the first draft; a second revision that resolved seven required
changes of a toolchain and git review and twenty-two of a workflow and rules review (including runtime experiments with
the real agent runtimes on instruction loading, hook registration, sandbox limits and the host's Qt environment under
AddressSanitizer); and a third revision that applied the maintainer's decisions (no machine account, no Apple
Developer ID, the second display reconnected for the final gate runs, the standing authorization, the Code of Conduct
contact) and replaced a non-strict required-check policy with strict checks and a trusted-base gate. The acceptance
checks of the scaffold, including those that need the maintainer's live session, Mac and GitHub account, are recorded in
the scaffold pull request.
