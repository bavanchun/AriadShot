## Behaviour

<!-- What changes for the user or the contributor, in one or two sentences. -->

## Ledger lines

<!-- Parity-ledger lines delivered or changed, one per line, or "none". -->

## MacShot references

<!-- The MacShot source lines this change follows, as macshot/<path>:<line>@b4d4f3a, or "none". -->

## Evidence

<!-- Tests added and their results, golden diffs for visual changes, run logs from nested Hyprland, the live host or
the Mac where CI cannot run the test. -->

## Risks

<!-- What could break, and how the change limits it. -->

## Checklist

- [ ] `cmake --workflow --preset dev` and `cmake --workflow --preset asan` pass, and `scripts/check-all.sh` is green
- [ ] Tests assert MacShot values from the ledger, not AriadShot constants
- [ ] Module boundaries hold (core is QtCore only; render and ui are QtGui only; platform code stays in backends/ and hosts/)
- [ ] Every intentional difference from MacShot has a deviation-register entry
- [ ] SPDX headers present; `PROVENANCE.md` updated for MacShot-derived material
- [ ] User-visible strings go through `tr()`; accessibility names set where the change adds controls
- [ ] Docs updated where behaviour, workflow or contracts changed
- [ ] Title follows Conventional Commits; no AI attribution, and no plan, ledger or gate IDs in commits
- [ ] External contributors: every commit is signed off (`git commit -s`)
