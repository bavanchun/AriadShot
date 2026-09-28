<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 13. Build, CI and Release

Part of the [AriadShot technical specification](README.md). Contributor-facing build instructions are in
`docs/dev/building.md`; the executable owners are `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `scripts/` and
`.github/workflows/`. When they and this file disagree, fix one of them in the same pull request. Decisions:
[ADR 0001](../adr/0001-architecture-and-stack.md), [ADR 0002](../adr/0002-foundation-and-workflow.md).

## 1. Toolchain

| Item | Linux | macOS |
| :--- | :--- | :--- |
| Language | C++20, no compiler extensions | C++20, Objective-C++ for Apple APIs, Swift only where Apple offers no Objective-C API (Translation) |
| Compilers | GCC (the rolling distribution default) and Clang in CI; GCC 13 on the Qt-floor job | the runner's Apple Clang |
| Build | CMake ≥ 3.28, Ninja | CMake ≥ 3.28, Ninja (Homebrew) |
| Qt | ≥ 6.8 (the floor); the Arch jobs roll with the distribution; the Qt-floor job pins Qt 6.8.4 | pinned Qt 6.8.4 in CI; a release pins one Qt minor ([§9](#9-versioning-changelog-and-releases)) |
| Deployment target | — | macOS 14.0 |

CI is the arbiter of which C++20 library features are usable: a feature that fails on any required job is not used.
`std::expected` is C++23 and is not available under `-std=c++20`; fallible APIs use `ariadshot::Expected<T, E>`, an
alias for vendored `tl::expected` (CC0-1.0), added with the first fallible API. A later move to C++23 replaces only the
alias.

## 2. CMake structure and presets

- `cmake_minimum_required(VERSION 3.28)`, generator Ninja, `project(AriadShot VERSION <x.y.z> LANGUAGES CXX)`.
  `OBJCXX` and Swift are enabled by the macOS backend's own `CMakeLists.txt` when it lands.
- `find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)`, plus `Test` when `BUILD_TESTING` is on;
  `qt_standard_project_setup(REQUIRES 6.8)`.
- **Optional components are explicit.** Every other dependency (DBus, ShaderTools, LinguistTools, NetworkAuth,
  Multimedia, Svg, Quick, GuiPrivate, layer-shell-qt, FFmpeg, PipeWire, the ML runtimes) is found by the module that
  needs it, behind an option that defaults to OFF and uses `REQUIRED` when ON. A missing dependency is a configure
  error, never a silent disable. This is what lets the scaffold configure with only Qt Core, Gui, Widgets and Test.
- **No auto-detection that changes outputs**: no `find_program` for linkers or compiler launchers, no global
  `CMAKE_POLICY_VERSION_MINIMUM`, no forced cache variables. Launchers and linkers belong in a developer's own
  `CMakeUserPresets.json` (ignored by git) or the CI environment.
- Module targets and the architecture check: [02](02-modules-and-interfaces.md).
- `.clangd` points at `build/dev` for the compilation database; no symlink in the source tree.

**Presets.** `CMakePresets.json` (schema 8) defines configure, build, test and workflow presets with the same names;
`binaryDir` is `build/<preset>`.

| Preset | Compiler | Build type | Extras | Test filter |
| :--- | :--- | :--- | :--- | :--- |
| `dev` | platform default | Debug | compile commands exported | excludes `wayland`, `nested-hyprland`, `live` |
| `dev-clang` | Clang | Debug | same | same |
| `asan` | Clang | Debug | `ARIADSHOT_SANITIZE=address,undefined` | same |
| `tsan` | Clang | Debug | `ARIADSHOT_SANITIZE=thread` | `tsan-safe` only |
| `release` | platform default | RelWithDebInfo | hardening (§4) | same as `dev` |
| `ci-dev`, `ci-asan`, `ci-tsan` | as their parent | as their parent | add `ARIADSHOT_WERROR=ON` and `CMAKE_COMPILE_WARNING_AS_ERROR=ON` | as their parent |

`cmake --workflow` accepts no `-D` arguments, so warnings-as-errors lives in the `ci-*` presets. **Every CI build job
runs exactly one command, `cmake --workflow --preset ci-<x>`**, and the same command reproduces the job locally. Local
`dev` builds do not fail on warnings introduced by a newly upgraded rolling compiler; CI does.

**Version string.** `project(VERSION)` is the source of truth. `cmake/AriadShotVersion.cmake` regenerates
`build/<preset>/generated/core/Version.h` at build time. The string is `PROJECT_VERSION` on a commit tagged
`v<PROJECT_VERSION>`, `PROJECT_VERSION+g<short-hash>` on any other commit, and plain `PROJECT_VERSION` without a `.git`
directory (source tarballs). There is no `-dirty` suffix. `ariadshot-daemon --version` prints it; a unit test asserts
that it starts with `PROJECT_VERSION`.

## 3. Dependencies

### 3.1 Two phases

| Phase | Network | What happens |
| :--- | :--- | :--- |
| Acquisition | allowed, explicit, pinned | installing distribution packages; the pinned Qt through `install-qt-action`; the pinned clang-format wheel; fetching the pinned corpus commit |
| Configure, build, test | **not allowed** | `FetchContent`, `ExternalProject`, `file(DOWNLOAD)` and `execute_process` calls to network tools are banned in CMake; scripts invoked by the build may not call network tools |

`scripts/check-no-network-build.sh` enforces the ban in CI. It is a policy check, not a guarantee: no job isolates the
network yet. Linux CI jobs will run the configure, build and test phase under `unshare --net` once that is proven to
work in the CI container [HYPOTHESIS]. Distribution builds (AUR, Flatpak) forbid build-time downloads as well, which is
why the "find the system package, else download" pattern is rejected: it still downloads when the package is missing.

### 3.2 Sources

- **System packages.** The Linux reference is Arch Linux: system Qt, FFmpeg, PipeWire, `layer-shell-qt` and ONNX
  Runtime (`onnxruntime-cpu`). The Arch CI jobs roll with the distribution; a weekly scheduled run on `main` catches
  breakage early.
- **Pinned Qt in CI.** Qt 6.8.4 through `install-qt-action` (pinned by commit SHA) on Ubuntu 24.04 and on macOS.
  Homebrew's `qt` formula moves with every release and is not used for Qt in CI; Homebrew still provides `ninja`.
  Local macOS development may use Homebrew `qt`. Each job prints the resolved Qt, compiler and CMake versions into the
  job summary, and configure fails when the cache variable `ARIADSHOT_EXPECT_QT_VERSION` (set by the job) does not match
  `Qt6_VERSION`.
- **Vendored code and data** that no distribution packages live in `third_party/<name>/`, each with its `LICENSE` and
  an `ORIGIN.md` (URL, version, commit): Wayland protocol XML for image-copy, data-control, screencopy, virtual pointer,
  Hyprland toplevel export and global shortcuts (with G2; generated by the system `wayland-scanner`); fonts (Inter,
  Noto Color Emoji, OFL; with G3); `tl::expected` (with the first fallible API); pocketfft (with scroll capture, M2).
- **Floors** are declared in `find_package` calls; the exact versions used for each gate are recorded in that gate's
  ADR.
- **macOS release bundling** (LGPL FFmpeg and Qt frameworks) is decided in M5; vcpkg remains a candidate for it.
- **Run-time downloads** exist only for consented ML models ([09](09-ml-services.md)) and the AppImage's OpenH264
  binary (§10), never during a build.

### 3.3 Dependency introduction by milestone

`docs/dev/building.md` owns the exact install commands. The set grows only when a gate or milestone needs it:

| When | Adds (Arch package names) |
| :--- | :--- |
| Scaffold | nothing beyond a C++ toolchain, CMake, Ninja and Qt (Core, Gui, Widgets, Test) |
| Local quality gates | `gitleaks`, `reuse`, `shellcheck`, `actionlint`; optional `ccache`, `clazy` |
| M0 Wayland lane | `layer-shell-qt`, `sway`, `wayland-utils` |
| M0 media lane | `vulkan-headers`, `vulkan-validation-layers`, `gpac` (test tool) |
| First release | `git-cliff` |
| M2 | `qt6-tools` (translations), `onnxruntime-cpu`, `libportal-qt6` if the portal backend uses it; CTranslate2 only after a verified toolchain build |
| M3–M4 | `whisper-cpp` |

## 4. Compiler options, hardening and sanitizers

All options come from the interface target `AriadShot::options` in `cmake/AriadShotCompilerOptions.cmake`.

- **Warnings** (GCC and Clang): `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
  -Wcast-qual -Wformat=2 -Wimplicit-fallthrough`; `-Werror` only with `ARIADSHOT_WERROR=ON`.
- **Qt definitions** for every AriadShot target: `QT_NO_KEYWORDS` (use `Q_EMIT`, `Q_SIGNALS`, `Q_SLOTS`),
  `QT_NO_CAST_FROM_ASCII`, `QT_NO_CAST_TO_ASCII` (forces `tr()` or `QStringLiteral`, so every user-visible string stays
  extractable for the 40 locales), `QT_NO_URL_CAST_FROM_STRING`, `QT_NO_NARROWING_CONVERSIONS_IN_CONNECT`,
  `QT_DISABLE_DEPRECATED_UP_TO=0x060800`.
- **Hardening** in `release` on Linux: `_FORTIFY_SOURCE=3`, `_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
  `-fstack-clash-protection`, `-fcf-protection` on x86-64, PIE, `-Wl,-z,relro,-z,now`. macOS hardening (libc++ hardening
  mode, optional hardened runtime, §7) is configured and verified in M5.
- **Sanitizers**: `ARIADSHOT_SANITIZE` accepts `address,undefined` or `thread`; test policy is in
  [12 §12](12-testing-strategy.md). Fuzz targets use libFuzzer with Clang and arrive with the first parser of untrusted
  input ([11](11-security-privacy.md)).

## 5. Formatting, linting and repository checks

| Check | Tool and rule | Where it runs |
| :--- | :--- | :--- |
| Formatting | **clang-format 22.1.8 exactly** (the PyPI wheel in CI). `.clang-format`: LLVM base, 4-space indent, 120 columns, left pointer alignment, attached braces, `Q_EMIT` as a statement-attribute-like macro, include blocks own header → project → Qt → standard and system. One version everywhere prevents formatting churn between contributors | pre-commit hook, `lint` |
| Static analysis | clang-tidy 22 with curated `bugprone-*`, `clang-analyzer-*`, `performance-*`, `modernize-*`, selected `cppcoreguidelines-*` and `misc-*`, and `readability-identifier-naming` configured to the naming rules of [02 §3](02-modules-and-interfaces.md); `WarningsAsErrors: '*'`. The exact list lives in `.clang-tidy` | `linux-clang` |
| Qt misuse | clazy levels 0 and 1 | `linux-clang` |
| Scope of analysis | tracked translation units under `src/` and `tests/` (`git ls-files`), header filter on the source tree, so generated MOC output is never analysed | `linux-clang` |
| Module boundaries | `scripts/check-architecture.sh` and the configure-time link check ([02 §2](02-modules-and-interfaces.md)) | pre-commit hook, `lint`, every configure |
| Network-free build | `scripts/check-no-network-build.sh` | `lint` |
| Repository hygiene | `scripts/check-repo.sh`: no private paths in the published content or file paths (the index in the pre-commit hook, the commit's tree in `lint`), no `CLAUDE.md`, `CLAUDE.local.md`, `.claude/CLAUDE.md` or `GEMINI.md` (tracked or untracked), every nested `AGENTS.md` listed in the root file, the owner gate embedding the current `scripts/github/gate-inspect.sh` | pre-commit hook, `lint` |
| Shell and workflows | `shellcheck`, `actionlint` | `lint` |
| Licence headers | `reuse lint` | `lint` |
| Secrets | `gitleaks` (distribution package, one version for hooks and CI) over staged changes, the pushed range and the pull request range | hooks, `lint` |
| Commit messages | `scripts/check-commit-message.sh`: Conventional Commits, header ≤ 100 characters (pull request titles ≤ 92), no AI references or attribution trailers, no plan, ledger, deviation, gate or milestone identifiers | `commit-msg` hook, `commit-policy` |
| Pull request size | `scripts/check-pr-size.sh`: 500 changed lines, excluding goldens, generated ledger lines, `third_party/` and generated protocol code; `size/exception` label set by the owner | `commit-policy` |
| DCO | `scripts/check-dco.sh`: `Signed-off-by` required on every commit by an external contributor; owner-account and Dependabot pull requests are exempt; the trailers are printed for the owner to copy into the squash commit body | `commit-policy` |

`scripts/check-all.sh` runs every local check in CI order, using the checkers and `.gitleaks.toml` of its own
directory on the repository in the current directory (§6.1, "Trusted checkers"). Git hooks live in `.githooks/` and
call the same scripts (activated once per clone by `scripts/setup-dev.sh`); there is no pre-commit framework and no
Node toolchain.

## 6. Continuous integration

### 6.1 Workflows

| Workflow | Triggers | Purpose |
| :--- | :--- | :--- |
| `ci.yml` | `pull_request`; `push` to `main`; weekly schedule on `main` | build and test matrix, lint |
| `pr-policy.yml` | `pull_request` (opened, synchronize, edited, labeled, unlabeled) | the `commit-policy` job |
| `owner-gate.yml` | `pull_request_target` (opened, synchronize, reopened, ready_for_review), so the definition on `main` always runs, never a pull request's copy | jobs `inspect` (fails closed: a fork pull request that changes `.github/workflows/` gets `owner-consent` = failure) and `owner-approval` (bound to the `owner-review` environment; after the owner's approval it creates the check run `owner-consent` = success on the head SHA); `permissions: {pull-requests: read, checks: write}`; no checkout, no repository code, no secrets; allowed to run by the repository's Actions event policy (§6.3) |
| `macos-vm.yml` | `workflow_dispatch`; schedule on `main` | the macOS 14 VM self-hosted runner (from G7), environment `owner-mac`; never `pull_request` or `pull_request_target` |
| release workflows (M5) | tags, manual | artifact builds without restored caches, in environments that require the owner's approval |

Common rules: `permissions: contents: read` unless a job needs more; no `pull_request_target` except the owner gate, which checks out nothing; no secrets in pull
request jobs; every action pinned by commit SHA; `concurrency` cancels superseded pull request runs; Dependabot updates
`github-actions` only.

**Range-aware checks.** `lint` and `commit-policy` check out with full history, take the range from the event
(pull request base and head SHAs, or `before` and `after` on push), verify both commits exist and **fail closed** if
the range cannot be resolved. Pull request titles and bodies are passed through `env:`, never interpolated into
scripts.

**Trusted checkers.** A pull request must not be judged by checkers it can edit. `lint` and `commit-policy` check out
the commit under test into `candidate/` and the trusted commit into `trusted/` (the pull request's base SHA; on `main`,
the same commit), both with `persist-credentials: false`, and run `trusted/scripts/` with `candidate/` as the working
directory. A change to a checker therefore applies from the next pull request on. **Bootstrap exception (one-time,
reviewed by the owner):** when the trusted commit has no checkers and the pull request's base SHA equals the
repository's first commit (`bbf2d44…`, pinned as `BOOTSTRAP_BASE` in both workflows), the pull request's own checkers
run and the job summary says so; any other base without checkers, including any other root commit, fails the job. Workflow
files run from the pull request's head and are protected instead by who can change them: agent credentials have no
Workflows permission, and a fork pull request that changes `.github/workflows/` fails the owner gate.

### 6.2 Jobs

The job names below are the required status checks; `owner-consent` is a check run created by the owner gate rather than a job name.

| Job | Runner | Does |
| :--- | :--- | :--- |
| `lint` | Ubuntu 24.04 with an `archlinux:base-devel` container | formatting, architecture, network-free build, repository hygiene, shellcheck, actionlint, `reuse lint`, gitleaks on the range |
| `commit-policy` | Ubuntu 24.04 | pull request title and body, every commit in the range, pull request size, DCO |
| `linux-gcc` | Arch container | `cmake --workflow --preset ci-dev` (GCC, Qt from Arch); tier 2 joins once proven ([12 §2](12-testing-strategy.md)) |
| `linux-clang` | Arch container | `ci-asan` and `ci-tsan` workflows, clang-tidy and clazy on the tracked file list |
| `linux-qt-floor` | Ubuntu 24.04 | GCC 13 and pinned Qt 6.8.4, `ci-dev` workflow, expected-Qt check |
| `macos` | `macos-26` (arm64) | pinned Qt 6.8.4, `ci-dev` workflow, deployment target 14.0, expected-Qt check |
| `macos-14` | `macos-14` (arm64) | as `macos`; the only per-pull-request check of the macOS 14 runtime. Required until GitHub retires the image on 2026-11-02; then the scheduled VM run replaces it and the parity report marks per-pull-request macOS 14 coverage as ended |
| `owner-consent` | Ubuntu 24.04 | check run created on the pull request's head SHA by `owner-gate.yml` (§6.1) after the owner approves the `owner-review` environment for that commit |

**Caching.** `ccache` per job keyed by job, compiler and a hash of the CMake files and presets (`CCACHE_BASEDIR` at the
checkout root), the pacman package cache in the container, and `install-qt-action`'s own cache. Caches are untrusted
input: they hold only compiler objects and packages, never credentials or signing material; pacman verifies package
signatures on install; release jobs build without restored caches.

### 6.3 Repository protection

Configured by the owner-run `scripts/github/setup-repository.sh`, which prints the active rules back:

- **`main` ruleset**: deletion restricted, non-fast-forward blocked, linear history, pull request required with zero
  approvals and conversation resolution, squash as the only merge method, the required checks of §6.2 each pinned to the
  GitHub Actions app, **strict** (`strict_required_status_checks_policy: true`: the branch must be up to date with `main`, so updating it starts a new gate run and needs a new owner approval, and the approved head always contains the current `main`), **no bypass actors**. No signed-commit rule and no code-owner rule.
- **`release-tags` ruleset** on `refs/tags/v*`: creation, update and deletion restricted, no bypass actors. Tags are
  created only by the owner-run `scripts/github/release-tag.sh` (§9).
- **Actions event policy** `owner-gate-pull-request-target`: applies to `.github/workflows/owner-gate.yml` only and
  allows only `pull_request_target`. GitHub blocks that event in public repositories by default from 2026-11-02, and
  this policy is what lets the owner gate keep running; `scripts/github/verify-identity.sh owner` checks it. If GitHub
  offers no such policy for the repository, the owner merges every pull request personally until the gate is
  redesigned.
- **Owner gate**: environment `owner-review` with the owner as required reviewer, self-review allowed, administrator
  bypass disabled. Agent credentials have no permission to approve deployments, set commit statuses, rerun workflows,
  edit workflows or change settings, so no pull request merges before the owner approved its exact head commit. What
  GitHub cannot enforce with a single account (merging after approval, releases on an existing tag) is covered by the
  agent guard and the rules in `docs/dev/agent-workflow.md`.
- **Settings**: squash merge only with the pull request title as the commit title and an empty body; branches deleted
  on merge; secret scanning and push protection on; Actions "Require approval for all external contributors" on; Private Vulnerability Reporting on; wiki and projects off.

## 7. macOS distribution

AriadShot has no paid Apple Developer account and never will (owner decision, ADR 0002). macOS releases are therefore
**ad-hoc signed and never notarized**. This narrows the architecture's original "notarized DMG" plan and is registered
as DEV-40 ([14 §5](14-gates-and-milestones.md#5-initial-deviation-register)).

- **Bundle.** An `.app` with the Qt frameworks and an LGPL FFmpeg build as frameworks (VideoToolbox for encoding),
  dynamically linked so users can relink the LGPL parts. Deployment target 14.0.
- **Signing.** Inside-out: every bundled framework and dylib first, then the app, with `codesign --sign -`; never
  `--deep` as the signing step. `codesign --verify --deep --strict` is the check. Apple Silicon refuses unsigned arm64
  code, and an ad-hoc signature satisfies that. The hardened runtime is optional without notarization and is enabled only
  if G7 shows it costs nothing.
- **First open.** A downloaded, un-notarized app is blocked by Gatekeeper on first open. On macOS 15 and later the user
  opens it once, then chooses **System Settings → Privacy & Security → Open Anyway**; macOS 14 still accepts
  Control-click → Open. The install page (`docs/user/install-macos.md`, M5) shows both paths with screenshots and asks the
  user to move the app to `/Applications` first. AriadShot never tells users to disable Gatekeeper or strip quarantine
  attributes, and no script it ships does so.
- **Homebrew.** No official cask: casks that fail Gatekeeper checks are no longer accepted in the official tap. An own
  tap (`bavanchun/homebrew-tap`) is possible at M5 as an owner-approved publishing action; its cask carries no
  quarantine-stripping step, so installing from it still meets Gatekeeper on first open.
- **Updates.** Sparkle 2 with **EdDSA signatures only**. Sparkle accepts an update when either the EdDSA signature or
  Apple code signing validates, rejects an update that drops code signing entirely (ad-hoc is the minimum), and removes
  the quarantine attribute from the installed update, so Gatekeeper asks only on the first install. The appcast and
  archives are served over HTTPS from GitHub Releases. The EdDSA private key is generated and kept by the owner on the
  Mac (login keychain plus an offline backup), never in CI or reachable by agents. Key rotation would require Apple code
  signing and is therefore unavailable: a lost or leaked key means users reinstall manually, which `SECURITY.md` states.
- **Privacy permissions after updates.** With an ad-hoc signature the code's designated requirement is its hash, which
  changes with every build, so Screen Recording and Accessibility grants stop matching after an update. The M5 update
  flow shows a one-time "grant Screen Recording again" prompt when the grant no longer matches. G7 measures whether a
  free self-signed project certificate keeps grants across updates; if it does, the owner decides whether releases are
  signed with it instead ([14 §2](14-gates-and-milestones.md#2-m0-gates)). Either way the app stays un-notarized.
- **DMG.** AriadShot's own art and layout (DEV-01); no MacShot DMG artwork.

## 8. Licensing and provenance

- **Licence.** Every AriadShot file is GPL-3.0-only: MacShot grants GPLv3 without "or later", so the combined work can
  never be "or later", and one identifier everywhere removes the risk of mislabelling MacShot-derived code.
- **SPDX via REUSE.** Every file carries `SPDX-FileCopyrightText: 2026 The AriadShot Authors` and
  `SPDX-License-Identifier: GPL-3.0-only`; files that cannot carry comments (PNG, JSON, JSON Lines, some templates) are
  covered in `REUSE.toml`. `reuse lint` runs in CI.
- **MacShot-derived material** (converted translations, ported algorithms, copied tables and constants) adds
  `SPDX-FileCopyrightText: sw33tLie and MacShot contributors` and a provenance line naming the MacShot paths at
  `b4d4f3a`. `PROVENANCE.md` lists every such file with its MacShot source paths; it ships in the source tree and in
  every package, with the notice "Copyright © sw33tLie and MacShot contributors", the GPLv3 text and a link to the pinned
  commit. It is updated in the same pull request that adds the material.
- **Not reused from MacShot**: the name, logo, DMG art, capture sound, the embedded imgbb key and the Google OAuth
  client (DEV-01, DEV-31).
- **Third-party licences.** Vendored files keep their own licence (LGPL, BSD, MIT, CC0, OFL, ISC are compatible;
  LGPL is copyleft, not permissive). Qt modules are LGPL-3.0 except **Qt Network Authorization, which is GPL-3.0 or
  commercial** (compatible with AriadShot, but it removes any LGPL-only distribution option). `layer-shell-qt` is
  LGPL-2.0-or-later with GPL-2.0-or-later parts; QtKeychain is BSD-3-Clause; the Linux icon set is Lucide-based (ISC);
  Inter and Noto Color Emoji are OFL. Model **weights** licences are recorded per catalogue entry
  ([09](09-ml-services.md)); CC BY-NC and AGPL models are excluded from defaults.
- **FFmpeg per package.** The AUR package links the distribution's FFmpeg (a GPL build including libx264), which is
  compatible. The AppImage bundles an FFmpeg built without `--enable-gpl` with the VA-API and NVENC encoders; its
  software H.264 fallback is Cisco's OpenH264 binary, downloaded at run time with consent so Cisco's patent licence
  covers it; libx264 is not bundled. The DMG bundles an LGPL FFmpeg and uses VideoToolbox. The exact configure lines of
  every bundled FFmpeg go into the release notes and `PROVENANCE.md`.
- **Distribution obligations.** Every package ships the GPL text, `PROVENANCE.md` and third-party notices. The AppImage
  and the DMG also carry a written offer for, or a link to, the complete corresponding source: the tagged AriadShot
  commit, the build scripts, and the source of each bundled library with its patches. Qt and FFmpeg stay dynamically
  linked in bundles.
- **Codec review.** A codec patent and distribution review (H.264, HEVC, AAC, AV1) is a release gate for the AppImage
  and the DMG, not for the AUR package, which uses the distribution's own FFmpeg.
- **Contributions.** DCO 1.1 sign-off for external contributors (§5); no CLA.

## 9. Versioning, changelog and releases

- **SemVer 0.x by milestone**: `v0.1.0` at M1 exit, `v0.2.0` at M2, `v0.3.0` at M3, `v0.4.0` at M4, `v1.0.0` at M5;
  `v0.N.P` for fixes between milestones. M0 publishes no release; it produces ADRs and measurements.
- **Changelog.** `CHANGELOG.md` is generated by git-cliff (`cliff.toml`) from the squash commit titles on `main`,
  grouped by Conventional Commit type including `build` and `revert`.
- **Procedure.**
  1. A `chore(release): …` pull request bumps `project(VERSION)`, pins the release's Qt minor, and regenerates
     `CHANGELOG.md`.
  2. The owner reviews, approves the owner gate and merges it.
  3. The owner runs `scripts/github/release-tag.sh`, which, with the owner's own credentials, temporarily adds the
     repository-admin role as a bypass actor on the `release-tags` ruleset, pushes the owner's SSH-signed annotated tag
     `vX.Y.Z`, and removes the bypass again.
  4. Release workflows build the artifacts for that milestone without restored caches, in an environment that requires
     the owner's approval, and attach them to a GitHub Release that the owner publishes.
- **Artifacts per milestone.** The AUR `-git` package tracks `main` from M1. AppImage, Flatpak and the macOS DMG with
  Sparkle arrive at M5. Every release note carries the four milestone artifacts ([14 §1](14-gates-and-milestones.md#1-milestone-model)).

## 10. Linux packaging

| Format | When | Contents and rules |
| :--- | :--- | :--- |
| AUR `ariadshot-git` | from the scaffold, tracking `main` from M1 | `packaging/arch/PKGBUILD`: `pkgver()` from `git describe`, `license=('GPL-3.0-only')`, `sha256sums=('SKIP')` (correct for a VCS source), dependencies on the system Qt and later FFmpeg, PipeWire, `layer-shell-qt` and `onnxruntime-cpu`; `check()` runs the offscreen suite with `QT_QPA_PLATFORMTHEME` and `QT_STYLE_OVERRIDE` unset and `-DBUILD_TESTING=ON` |
| AUR stable | from `v0.1.0` | the same recipe from release tags |
| AppImage | M5 | bundled Qt and LGPL FFmpeg as shared objects; OpenH264 as a consented run-time download; written source offer |
| Flatpak | M5 | a portal-only variant: the sandbox may block image-copy capture, layer-shell and Hyprland IPC, so the capability registry reports those as unavailable and the GNOME-style portal paths are used |

**Desktop integration files.** `packaging/linux/io.github.bavanchun.AriadShot.desktop` (`Exec=ariadshot-daemon %U`,
`Categories=Graphics;Utility;`); its `MimeType=` gains MacShot's eight image types when the image open route lands
(M1) and `x-scheme-handler/ariadshot` with the URL scheme; video types are not registered, as in MacShot
([08 §5](08-platform-integration.md#5-single-instance-control-socket-cli-and-url-scheme)).
An AppStream metainfo file arrives before the first stable package. The app ID `io.github.bavanchun.AriadShot` is used
for the desktop entry, AppStream, Flatpak, the portal app ID and the macOS bundle identifier. Launch at login is an XDG
autostart entry written by the application on user opt-in; there is no systemd user unit and no `X-systemd-skip` key.

**Updates on Linux** come from the package manager. "Check for Updates…" checks the release feed and links to the
package manager, without downloading anything (DEV-36, [10](10-upload-and-network.md)).

## 11. Build variants

| Variant | How | Effect |
| :--- | :--- | :--- |
| Standard | default options | every feature |
| Offline | the CMake option `ARIADSHOT_OFFLINE=ON` | removes upload and cloud code and disables model downloads (sideloading stays); update checks remain, against the offline variant's own update feed, as in MacShot ([10 §6](10-upload-and-network.md#6-offline-build)) |

MacShot ships both variants from one release pipeline; AriadShot's offline variant is a release deliverable from M5
(SH-25).
