<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# AriadShot documentation

## Product and specification

- [`prd.md`](prd.md): product requirements — vision, users, scope by milestone, functional and non-functional
  requirements, release criteria.
- [`spec/`](spec/README.md): technical specification for implementers — architecture, modules and interfaces,
  rendering contracts, capture, recording and studio, formats, storage, platform integration, machine learning,
  network, security, testing, build and release, gates and milestones.

## Decisions

Architecture decision records; each gate outcome of M0 is added as a new record.

- [`adr/0001-architecture-and-stack.md`](adr/0001-architecture-and-stack.md): the architecture and stack, platform
  strategy, gates, budgets, milestones, the deviation register and the MacShot parity digest.
- [`adr/0002-foundation-and-workflow.md`](adr/0002-foundation-and-workflow.md): how AriadShot is built — repository,
  toolchain, git and review workflow, rules and their enforcement.

## Contributor guides

- [`dev/building.md`](dev/building.md): tools, presets, sanitizers, macOS, the local checks.
- [`dev/coding-standards.md`](dev/coding-standards.md): C++20 and Qt rules, naming, ownership, errors, threading.
- [`dev/testing.md`](dev/testing.md): test tiers, labels, the neutral environment, golden images.
- [`dev/agent-workflow.md`](dev/agent-workflow.md): how the maintainer and coding agents work, and what enforces it.
- [`dev/review-checklist.md`](dev/review-checklist.md): what every review checks.
- [`dev/macos-checklist.md`](dev/macos-checklist.md): the manual checks on the maintainer's Mac.

Start with the repository's [`README.md`](../README.md), [`CONTRIBUTING.md`](../CONTRIBUTING.md) and
[`AGENTS.md`](../AGENTS.md).
