#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Owner-run: creates and pushes the SSH-signed annotated release tag vX.Y.Z (docs/spec/13-build-ci-release.md §9).
# The release-tags ruleset has no bypass actors, so this script, with the owner's own gh login:
#   1. checks that HEAD is origin/main, that project(VERSION) is X.Y.Z and that git signs tags with an SSH key;
#   2. adds the repository admin role as a bypass actor of release-tags and verifies that the owner can now bypass it;
#   3. creates, verifies and pushes the tag;
#   4. removes the bypass again, also when a step fails, and verifies that nobody can bypass the ruleset.
#
# Usage: release-tag.sh vX.Y.Z [--repo OWNER/NAME]

set -euo pipefail

# The repository admin role in ruleset bypass lists (as GitHub's ruleset export writes it). Step 2 verifies it.
readonly ADMIN_ROLE_ID=5

tag=${1:-}
repo=bavanchun/AriadShot
if [ "${2:-}" = --repo ]; then
    repo=${3:?}
fi
owner=${repo%%/*}
if [[ ! $tag =~ ^v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    echo "usage: $0 vX.Y.Z [--repo OWNER/NAME]" >&2
    exit 2
fi
version=${BASH_REMATCH[1]}

cd "$(git rev-parse --show-toplevel)"

login=$(gh api user --jq .login)
[ "$login" = "$owner" ] || { echo "release-tag: run this with the owner's ($owner) own gh login, not '$login'" >&2; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "release-tag: the working tree is not clean" >&2; exit 1; }
git fetch --quiet origin main --tags
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "release-tag: HEAD is not origin/main" >&2; exit 1; }
project_version=$(sed -nE 's/^project\(AriadShot VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
[ "$project_version" = "$version" ] ||
    { echo "release-tag: CMakeLists.txt declares $project_version, not $version" >&2; exit 1; }
[ "$(git config gpg.format || true)" = ssh ] && [ -n "$(git config user.signingkey || true)" ] ||
    { echo "release-tag: configure SSH signing (git config gpg.format ssh; git config user.signingkey ...)" >&2; exit 1; }
! git rev-parse -q --verify "refs/tags/$tag" >/dev/null || { echo "release-tag: $tag already exists" >&2; exit 1; }

ruleset_id=$(gh api "repos/$repo/rulesets" --jq '.[] | select(.name == "release-tags") | .id')
[ -n "$ruleset_id" ] || { echo "release-tag: the release-tags ruleset does not exist; run setup-repository.sh" >&2; exit 1; }

set_bypass() {
    gh api "repos/$repo/rulesets/$ruleset_id" |
        jq --argjson actors "$1" '{name, target, enforcement, conditions, bypass_actors: $actors,
            rules: [.rules[] | {type} + (if .parameters then {parameters} else {} end)]}' |
        gh api --method PUT "repos/$repo/rulesets/$ruleset_id" --input - >/dev/null
}

bypass_state() {
    gh api "repos/$repo/rulesets/$ruleset_id" --jq '.current_user_can_bypass'
}

restore() {
    if set_bypass '[]' && [ "$(gh api "repos/$repo/rulesets/$ruleset_id" --jq '.bypass_actors | length')" = 0 ]; then
        echo "release-tag: release-tags has no bypass actors again"
    else
        echo "release-tag: WARNING: could not remove the bypass from release-tags; remove it by hand now" >&2
        exit 1
    fi
}
trap restore EXIT

set_bypass "[{\"actor_id\": $ADMIN_ROLE_ID, \"actor_type\": \"RepositoryRole\", \"bypass_mode\": \"always\"}]"
state=$(bypass_state)
[ "$state" = always ] || { echo "release-tag: the temporary bypass did not apply (current_user_can_bypass: $state)" >&2; exit 1; }

git tag -s "$tag" -m "AriadShot $tag"
git verify-tag "$tag"
git push origin "refs/tags/$tag"
echo "release-tag: pushed $tag"
