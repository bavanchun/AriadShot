<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Contributing to AriadShot

Thank you for helping. AriadShot remakes MacShot; its first rule is fidelity: behaviour, strings, constants and
geometry come from MacShot's source at commit `b4d4f3a`, and an intentional difference needs a deviation-register entry.
Read [`AGENTS.md`](AGENTS.md) (it is short and binds humans too) and [`docs/dev/`](docs/README.md#contributor-guides).

## Set up

```sh
scripts/setup-dev.sh                 # activates the git hooks and lists missing tools; it installs nothing
cmake --workflow --preset dev        # build and test
scripts/check-all.sh                 # every check CI runs
```

To cite or port MacShot code, clone <https://github.com/sw33tLie/macshot>, check out `b4d4f3a`, and set
`ARIADSHOT_MACSHOT_DIR` to that checkout.

## Workflow

- `main` is always green. Work on a short-lived topic branch named `<type>/<topic>` and open a pull request against
  `main`. Pull requests are squash-merged by the maintainer.
- One pull request does one thing and changes at most 500 lines (goldens, generated ledger files and `third_party/` do
  not count); a larger one needs the maintainer's `size/exception` label.
- Tests come first and assert MacShot's values, cited as `macshot/<path>:<line>@b4d4f3a`.
- CI must pass on every required job. The maintainer reviews the pull request and approves the `owner-review`
  deployment, which creates the required `owner-consent` check on your head commit; each new push needs a new approval.
- Fill in the pull request template: behaviour, ledger lines, MacShot references, evidence and risks.

## Commit messages

[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/): `<type>(<scope>): <subject>`.

- Types: `feat fix perf refactor test docs build ci chore revert`.
- Scopes: `core render ui hosts windows platform wayland portal x11 macos media ml net app cli parity tests build ci
  docs packaging i18n deps repo release`; omit the scope only for repository-wide changes.
- The subject starts lower-case (identifiers such as `QPainter` are fine), has no final period, and the header is at
  most 100 characters (pull request titles at most 92).
- The body explains behaviour and reason. It carries no references to AI models or tools, no AI attribution, and no
  plan, ledger, deviation, gate or milestone identifiers (`SH-08`, `DEV-13`, `G1`, `M1`): those belong in issues and
  pull request descriptions.
- `scripts/check-commit-message.sh` enforces these rules in the `commit-msg` hook and in CI.

## Developer Certificate of Origin

Contributions are accepted under the [Developer Certificate of Origin 1.1](https://developercertificate.org/). If you
are an **external contributor**, sign off every commit with `git commit -s`, which adds
`Signed-off-by: Your Name <you@example.org>` with the address of the commit author. The `commit-policy` check verifies
it. Because squash merges replace your commits, the maintainer copies your sign-off lines into the body of the squash
commit, so they stay in `main`.

Pull requests from the maintainer's own account are exempt: that covers the maintainer's work and work done by coding
agents operated through the maintainer's account, which enters the project under the maintainer's responsibility.
Dependabot's pull requests are exempt too. Agents never add `Signed-off-by`, because only a person can certify the DCO.
There is no contributor licence agreement.

## Licence headers

Every file carries `SPDX-FileCopyrightText: 2026 The AriadShot Authors` and `SPDX-License-Identifier: GPL-3.0-only`
([REUSE](https://reuse.software/)); files that cannot hold a comment are covered in `REUSE.toml`. A file containing
material derived from MacShot also carries `SPDX-FileCopyrightText: sw33tLie and MacShot contributors`, a provenance
line naming the MacShot source at `b4d4f3a`, and an entry in [`PROVENANCE.md`](PROVENANCE.md) in the same pull request.

## Coding agents

Coding agents follow [`AGENTS.md`](AGENTS.md) and [`docs/dev/agent-workflow.md`](docs/dev/agent-workflow.md). They
never merge, tag, publish releases or change repository settings; the maintainer does.

## Reporting problems

Bugs and parity defects: use the issue forms. Security vulnerabilities: see [`SECURITY.md`](SECURITY.md); never open a
public issue for them.
