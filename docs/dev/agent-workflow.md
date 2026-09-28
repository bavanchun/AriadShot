<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# How the maintainer and coding agents work

Most AriadShot code is written by AI coding agents working in parallel under a manager agent, reviewed by an agent of
a different model family, and approved and merged by the maintainer. This page is the public form of that workflow; the
decisions behind it are in [ADR 0002](../adr/0002-foundation-and-workflow.md). The rules every agent must follow are in
[`AGENTS.md`](../../AGENTS.md).

## Roles

| Role | Does | Never does |
| :--- | :--- | :--- |
| Maintainer | approves each pull request's owner gate and merges it; pushes workflow changes; tags and releases; repository settings; host packages; live-session, capture and recording tests; the Mac checklist; accounts and credentials | — |
| Manager (an agent) | turns issues and plan phases into task packets; creates and removes worktrees; starts and monitors implementers and reviewers; integrates shared files; keeps the parity status; performs git operations for sandboxed agents; tells the maintainer when a pull request is ready | merge, tag, change settings, approve the owner gate, act as the maintainer, run live capture without approval |
| Implementer (an agent) | the packet's write set in its own worktree; tests first; local checks; commits, pushes its branch and opens the pull request | edit outside its write set, merge, touch the main checkout |
| Reviewer (an agent of another model family) | adversarial review of the diff and of the cited MacShot source, read-only; prints its report, which the manager saves and summarises on the pull request; verdict `PASS`, `PASS_WITH_GAPS` or `FAIL` | write files, build |
| Tester | the implementer runs the narrow tests and the local workflows; CI reruns everything independently; the maintainer runs live and Mac tests | mark ledger lines verified without evidence |

An agent that runs in a sandbox that cannot write the repository's shared git directory (a linked worktree keeps its
index under the main checkout's `.git/`) edits and tests only; the manager commits and pushes for it.

## The loop

```text
issue or plan phase → task packet → worktree → implement (tests first) → local checks → push → pull request → CI
→ cross-family review → fixes → maintainer review and owner-gate approval → squash merge (maintainer)
→ post-merge CI on main → parity status update → worktree removal (processes stopped)
```

1. **Packet.** The manager writes a task packet: the task and its ledger lines, the MacShot sources, the files to read,
   the files the agent may modify, acceptance criteria (tests to add and pass, commands), constraints, the worktree and
   branch, the report location and the status protocol. The agent receives only a short prompt pointing at the packet.
2. **Implement.** Tests first from the ledger values; the narrowest test first, then `cmake --workflow --preset dev`,
   then `scripts/check-all.sh`.
3. **Pull request.** The title is a Conventional Commit; the body follows the template (behaviour, ledger lines, MacShot
   references, evidence, risks) and carries no AI attribution.
4. **Review.** A reviewer of another model family reads the diff and the cited MacShot lines, spot-checking at least
   five non-trivial values. `FAIL` or a blocking finding returns the work to the implementer in the same worktree.
5. **Maintainer.** Reads the pull request, approves the `owner-approval` deployment (or asks for changes) and merges
   with squash. Live-session or Mac evidence, when needed, is attached before approval.
6. **Close.** The manager checks the post-merge run on `main`, updates the parity status, removes the worktree and
   closes the issue.

Every agent run ends with a status block:

```text
Status: DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT
Summary: one or two sentences
Concerns/Blockers: optional
```

`BLOCKED` and `NEEDS_CONTEXT` make the manager change the context, scope or approach; the same prompt is never simply
re-sent. Uncertainty is reported as `DONE_WITH_CONCERNS`, not hidden.

## Worktrees and panes

- One task = one worktree = one branch = one pull request. `scripts/worktree.sh create <branch> [--agent NAME]
  [--packet PATH]` fetches `origin/main` (serialised between parallel callers), adds the worktree in the directory named
  by `ARIADSHOT_WORKTREES_DIR` and records the owner in `<worktree>/.agent/owner`. Each worktree has its own `build/`.
- **Agents start at the worktree root.** Claude Code loads the project settings, and so the agent guard, only when a
  session starts at the project root; agy reads `.agents/hooks.json` from there.
- **Clean panes.** `scripts/worktree.sh shell <branch> [COMMAND]` runs the agent in a clean environment: only `HOME`,
  `PATH`, `TERM`, `LANG`, the four `ARIADSHOT_*` variables and `GH_CONFIG_DIR` pointing at the agent token's
  configuration; no shell profile, aliases or functions, so no wrapper can add a flag that disables a sandbox, and
  `GH_TOKEN` and `SSH_AUTH_SOCK` do not reach the agent. The first step of every packet is to report the agent's
  effective sandbox or permission mode and to stop with `BLOCKED` if it differs from the packet.
- **Processes.** Every long-running process a repository script starts (a nested compositor, the application under
  test) is recorded in `<worktree>/.agent/processes/<pid>` with its start time and command line, and stopped when its
  task ends. `scripts/worktree.sh remove <branch>` refuses a worktree with uncommitted changes or commits on no remote,
  stops only recorded processes whose PID, start time and command line still match, lists any other process working
  inside the worktree and refuses until the maintainer confirms, and never removes a worktree by force.
- **Parallelism.** Parallel tasks have disjoint *directory* write sets, checked by the manager before starting them. The
  shared files — the root `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `.github/`, `scripts/`, every `AGENTS.md`,
  every module `CMakeLists.txt` and each switch of a module from INTERFACE to STATIC — have one owner per task, by
  default the manager. By default at most three implementers and two reviewers run at once.
- Branches are rebased on `origin/main` before the maintainer's review; the manager resolves conflicts in shared files.

## What needs the maintainer

Always the maintainer's own action or explicit approval: approving the owner gate and merging; pushing changes to
`.github/workflows/`; tags and releases; repository settings, rulesets, environments, collaborators, tokens and
secrets; publishing anywhere else; host packages and anything with `sudo`; changes to the live desktop session
(monitors, compositor settings, headless outputs on the live compositor, portal grants, autostart of a build); live
capture and recording tests; the final measurement runs of the display gates, which need the second display connected
(agents ask and wait); starting a nested compositor window on the live session; external accounts and credentials;
deleting remote branches an agent did not create; anything a packet does not cover.

**Standing authorization** (accepted by the maintainer): agents may read anything in the workspace; create and remove
their own worktrees; build and run tests locally, including headless compositors started by repository scripts; run a
build on the live session for a smoke test that does not capture, record, register global shortcuts or enable
autostart (the maintainer is present for the first such run); push their own topic branches (except workflow changes),
open and update their own pull requests and issues, and comment with the agent token.

## What GitHub enforces

Agents act through the maintainer's GitHub account with a fine-grained token limited to this repository, expiring
within 90 days, with Contents, Pull requests and Issues read/write and Actions, Checks and Metadata read, and **without**
Administration, Deployments, Commit statuses, Workflows, Environments, Secrets or Variables.
`scripts/github/setup-repository.sh` (run by the maintainer) configures the protections below;
`scripts/github/verify-identity.sh agent` checks the token from an agent pane and `verify-identity.sh owner` checks the
configuration with the maintainer's login.

**The owner gate.** `.github/workflows/owner-gate.yml` runs on `pull_request_target`, so the definition on `main` runs,
never a pull request's copy; it checks out nothing and holds no secrets. Its `inspect` job fails a fork pull request
that changes `.github/workflows/`; otherwise `owner-approval` waits on the `owner-review` environment (required
reviewer: the maintainer; administrator bypass off) and, once approved, creates the check run `owner-consent` =
success on the pull request's head commit. Every push starts a new run that waits again. The `main` ruleset requires
`owner-consent` and the CI checks, all strict, so a head behind `main` cannot merge and updating it needs a new
approval.

| Action with the agent token | GitHub | Also covered by |
| :--- | :--- | :--- |
| Push to, force-push or delete `main` | **blocked** (ruleset without bypass) | agent guard, pre-push hook |
| Merge before the maintainer approved the head commit, with red CI, or with a head behind `main` | **blocked** (`owner-consent` and CI required, strict) | — |
| Merge with a method other than squash | **blocked** | — |
| Change settings, rulesets, environments, secrets or collaborators | **blocked** (no Administration) | agent guard |
| Approve the gate, set statuses, rerun approved runs, edit workflows | **blocked** (no Deployments, Commit statuses, Actions write or Workflows) | — |
| Create, move or delete a `v*` tag | **blocked** (tag ruleset without bypass) | agent guard |
| Merge a pull request after the maintainer approved its gate and CI is green | **allowed** (merging needs only Contents write) | agent guard denies `gh pr merge` and the merge API; rule "agents never merge" |
| Create or edit a release on an existing tag | **allowed** | agent guard denies `gh release` and the releases API; rule |
| Close or reopen pull requests and issues, labels, comments, delete other branches | **allowed** | rule: agents act only on their own pull requests and branches |
| Anything done with the maintainer's full login, SSH key or logged-in browser | **not distinguishable** from the maintainer | those credentials are absent from agent panes; Claude Code's project settings deny the browser-automation tools; rule |

So consent is enforced by the server up to the maintainer's approval of a commit; after it, "the maintainer merges" is
a rule backed by the guard. Because the approval binds the head commit, which already contains the current `main`, an
early merge by an agent can only merge exactly what was approved. If the gate ever behaves differently from this
description, the maintainer merges every pull request personally and this page is updated.

## The agent guard

`scripts/agent-guard/check-command.sh` holds the policy; `claude-hook.sh` (registered in `.claude/settings.json` for
the Bash tool and the edit tools) and `agy-hook.sh` (registered in `.agents/hooks.json`) translate each runtime's hook
format. It denies: pushes whose destination is `main` or `master` (including `refs/heads/main` and a bare push on
`main`), `--all` and `--mirror`; force pushes without `--force-with-lease`; tag pushes; `reset --hard`, `clean -f` with
`-d` or `-x`, `worktree remove --force`, `filter-branch`, `filter-repo`, `update-ref -d`; recursive removal of the
repository, its git directory or the MacShot tree; writes into `.git/` or the MacShot tree; `gh pr merge`, `gh release`
except `list` and `view`, `gh repo edit|delete|rename`, `gh secret`, `gh variable`, `gh ruleset` except reads, `gh auth`
except `status`, and write calls to the GitHub API's merge, release, tag, ruleset, environment, settings,
collaborator, hook, key, secret and deployment endpoints. Each denial names its rule. Without `jq` (or bash 4) the
adapters block only commands that push or `reset --hard` and warn about the rest. The cases are in
`tests/scripts/agent-guard-cases.tsv`.

**Limits.** The guard reads command text. It is a safety net for accidents, not a security boundary: an interpreter, a
script or another tool can bypass it, and a Claude Code session started outside the worktree root has no guard. The
controls that hold are the server-side rulesets, the token's missing permissions and the read-only MacShot tree.

## Instruction loading

`AGENTS.md` is the single instruction file for every runtime. Only Claude Code loads nested `AGENTS.md` files by itself
(after reading a file in that directory); Codex and agy load only the root file (agy walks from the working directory up
to the repository root), which is why the root file lists every nested file and `scripts/check-repo.sh` keeps that list
complete. Known exceptions:

- A `CLAUDE.md`, `.claude/CLAUDE.md` or `CLAUDE.local.md` in the working directory or any parent switches Claude Code to
  CLAUDE.md-only mode. Such files are forbidden here, including when a documentation or folder-context skill offers to
  create one, and `check-repo.sh` rejects them, tracked or not.
- Claude Code's Explore and Plan subagents skip project instructions, so their scouting output is not rule-aware.
- The first Claude Code session after an upgrade from a version older than 2.1.276 reads only `CLAUDE.md`.
- Personal global rule files reach only the runtime they belong to, so every rule that must bind all agents is written
  into this repository's `AGENTS.md`.

**Canary check after upgrading a runtime.** In a scratch repository with a root `AGENTS.md` naming a nested
`src/render/AGENTS.md`, each file containing a unique word, start the upgraded runtime at the repository root and ask
it for the words it knows before and after reading a file in `src/render/`. Update this section if the answer changes.

## Reports and handoffs

Packets, reviews, handoffs and plans are private records of the maintainer's workspace. The public repository sees the
pull request description and summaries of reviews. A handoff between sessions records the outcome, the branch and
worktree, the files changed, the exact verification commands with their results, and open questions.
