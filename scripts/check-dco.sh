#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Developer Certificate of Origin 1.1 for external contributors (CONTRIBUTING.md): every non-merge commit of the pull
# request carries a "Signed-off-by:" trailer with its author's e-mail address. Pull requests opened by the repository
# owner's account (the owner's own work and agent work done through it) and by Dependabot are exempt.
#
# Pull request branches are temporary and CI logs expire, so the sign-offs are printed (and added to the job summary
# in CI) for the owner to paste into the squash commit body when merging; that keeps them in main.
#
# Usage: check-dco.sh --range BASE..HEAD --author LOGIN --owner LOGIN

set -uo pipefail

range=""
author=""
owner=""
while [ $# -gt 0 ]; do
    case "$1" in
        --range) range=${2-}; shift 2 ;;
        --author) author=${2-}; shift 2 ;;
        --owner) owner=${2-}; shift 2 ;;
        *) echo "usage: $0 --range BASE..HEAD --author LOGIN --owner LOGIN" >&2; exit 2 ;;
    esac
done
if [ -z "$author" ] || [ -z "$owner" ]; then
    echo "usage: $0 --range BASE..HEAD --author LOGIN --owner LOGIN" >&2
    exit 2
fi

if [ "$author" = "$owner" ] || [ "$author" = "dependabot[bot]" ]; then
    echo "check-dco: pull request by $author is exempt (repository owner or Dependabot)"
    exit 0
fi

base=${range%%..*}
head=${range##*..}
if [ -z "$base" ] || [ -z "$head" ] || [ "$range" = "$base" ] ||
    ! git cat-file -e "$base^{commit}" 2>/dev/null || ! git cat-file -e "$head^{commit}" 2>/dev/null; then
    echo "check-dco: cannot resolve the range '$range'" >&2
    exit 2
fi

missing=0
trailers=""
while read -r commit; do
    email=$(git log -1 --format=%ae "$commit" | tr '[:upper:]' '[:lower:]')
    commit_trailers=$(git log -1 --format='%(trailers:key=Signed-off-by,valueonly,separator=%x0A)' "$commit")
    signed=0
    while IFS= read -r trailer; do
        [ -n "$trailer" ] || continue
        trailers+="Signed-off-by: $trailer"$'\n'
        trailer_email=$(sed -nE 's/.*<([^>]+)>.*/\1/p' <<<"$trailer" | tr '[:upper:]' '[:lower:]')
        [ "$trailer_email" = "$email" ] && signed=1
    done <<<"$commit_trailers"
    if [ "$signed" = 0 ]; then
        echo "check-dco: commit ${commit:0:12} has no Signed-off-by trailer for its author <$email> (use git commit -s)"
        missing=1
    fi
done < <(git rev-list --no-merges "$base..$head")

distinct=$(printf '%s' "$trailers" | awk 'NF && !seen[$0]++')
if [ -n "$distinct" ]; then
    echo "check-dco: sign-offs to paste into the squash commit body:"
    printf '%s\n' "$distinct"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        {
            echo "### DCO sign-offs for the squash commit body"
            echo
            echo '```text'
            printf '%s\n' "$distinct"
            echo '```'
        } >>"$GITHUB_STEP_SUMMARY"
    fi
fi
exit "$missing"
