<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# Provenance of material reused from MacShot

AriadShot remakes MacShot by sw33tLie and the MacShot contributors (<https://github.com/sw33tLie/macshot>). This file
lists every file in this repository that contains material derived from MacShot at commit
[`b4d4f3a`](https://github.com/sw33tLie/macshot/tree/b4d4f3a): converted translations, ported algorithms, copied
tables, strings and named constants. It ships in the source tree and in every package.

## Licence and notice

MacShot is licensed under the GNU General Public License version 3 without an "or later" option, so the material below,
and AriadShot as a whole, is licensed GPL-3.0-only ([`LICENSE`](LICENSE)). MacShot's Swift files carry no copyright
headers; the notice for MacShot material is:

> Copyright © sw33tLie and MacShot contributors. Licensed under the GNU General Public License version 3. Source:
> <https://github.com/sw33tLie/macshot/tree/b4d4f3a>.

AriadShot does not reuse MacShot's name, logo, DMG artwork, capture sound, embedded imgbb key or Google OAuth client.

## Rules

- A pull request that adds MacShot-derived material adds its row here in the same change.
- Each such file carries `SPDX-FileCopyrightText: sw33tLie and MacShot contributors` next to the AriadShot line, and a
  provenance comment naming the MacShot source as `macshot/<path>:<line>@b4d4f3a`.
- A citation that only points at MacShot behaviour, without copying material, needs no row.
- Vendored third-party files keep their own licences and are described in `third_party/<name>/ORIGIN.md`.

## Files

| File | MacShot source at `b4d4f3a` | Material |
| :--- | :--- | :--- |
| `src/app/TrayController.cpp` | `macshot/AppDelegate.swift:914` | the status menu's Quit item title, with AriadShot's name in place of MacShot's |
| `tests/unit/app/tst_TrayController.cpp` | `macshot/AppDelegate.swift:914` | the same title, as the expected value |
| `src/render/effects/Blur.cpp` | `macshot/Model/Annotation.swift:2253-2272` | the censor blur's sigma rule: at least 10, and 3 % of the region's shorter side |
| `tests/unit/render/tst_Blur.cpp` | `macshot/Model/Annotation.swift:2256` | the same sigma rule, as expected values |
| `src/render/effects/Pixelate.cpp` | `macshot/Model/Annotation.swift:1953-2020` | the three step sizes: reduce by 8, then by 2, enlarge to twice the region's size in points |
| `tests/unit/render/tst_Pixelate.cpp` | `macshot/Model/Annotation.swift:1990-2015` | the same step sizes, as expected values |
