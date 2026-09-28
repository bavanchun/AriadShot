<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Security policy

## Reporting a vulnerability

Report vulnerabilities privately through GitHub's private vulnerability reporting:
<https://github.com/bavanchun/AriadShot/security/advisories/new>. Do not open a public issue, pull request or
discussion about a vulnerability before a fix is available.

Include what an attacker can do, the affected version (`ariadshot-daemon --version`) and platform, and steps or a file
that reproduce the problem. Leave out screenshots or recordings that show personal data.

## Scope

In scope: AriadShot's own code in this repository, its build and release scripts, and the packages built from it.
Examples: memory-safety defects in parsers of clipboard content, images, settings imports, history or project files;
the control socket and the `ariadshot://` URL scheme; credential storage; the update check; the agent guard and the
repository checks.

Out of scope: vulnerabilities in MacShot itself (report them upstream), in Qt, FFmpeg or other dependencies (report them
to their projects; tell us if AriadShot needs to react), and issues that require an attacker who already controls the
user's account.

## Response

AriadShot is maintained by one person in their spare time. Reports are handled on a best-effort basis: we aim to
acknowledge a report and share an assessment as soon as we can, but we make no guarantee about response or fix times.
Fixes are released as soon as they are ready, and the advisory is published with the fix.

## Supported versions

There is no release yet. Once releases exist, only the latest release receives security fixes.

## macOS update signing

macOS builds are ad-hoc signed and not notarized, and their updates are verified with an EdDSA signature. That key
cannot be rotated without an Apple Developer ID, which the project does not hold: if it is ever lost or exposed, users
will be told to reinstall AriadShot manually from the release page instead of updating in place.
