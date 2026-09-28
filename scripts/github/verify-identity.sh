#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Read-only checks of the credentials and protections that keep agents from merging, tagging or changing settings
# (docs/dev/agent-workflow.md, "What GitHub enforces"). Nothing here writes to GitHub.
#
#   verify-identity.sh agent [--repo OWNER/NAME] [--pr NUMBER]
#       Run in an agent pane, with the agent token. Checks that the token acts as the owner's account, is a
#       fine-grained token that expires (warning 14 days before), has no Administration permission (the
#       Administration-only endpoint answers 403), and can bypass no ruleset. With --pr, checks that the pull request
#       is blocked while the owner gate waits. Finally asks the owner to confirm the token's grants on its settings
#       page, because no API lists them.
#   verify-identity.sh owner [--repo OWNER/NAME]
#       Run with the owner's own login. Checks the owner-review environment, both rulesets, the strict required
#       checks and the approval setting for workflow runs from forks.
#
# Prints PASS, WARN or FAIL per check; exits 1 when a check fails.

set -uo pipefail

mode=${1:-}
shift || true
repo=bavanchun/AriadShot
pr=""
while [ $# -gt 0 ]; do
    case "$1" in
        --repo) repo=${2-}; shift 2 ;;
        --pr) pr=${2-}; shift 2 ;;
        *) mode=invalid; break ;;
    esac
done
owner=${repo%%/*}
failures=0

pass() { echo "PASS: $1"; }

warn() { echo "WARN: $1"; }
fail() {
    echo "FAIL: $1"
    failures=$((failures + 1))
}

check_login() {
    local login
    login=$(gh api user --jq .login 2>/dev/null)
    if [ "$login" = "$owner" ]; then
        pass "the credential acts as $owner"
    else
        fail "the credential acts as '${login:-nobody}', expected $owner"
    fi
}

verify_agent() {
    check_login

    local headers expiry
    headers=$(gh api --include user 2>/dev/null | tr -d '\r')
    expiry=$(sed -nE 's/^[Gg]ithub-[Aa]uthentication-[Tt]oken-[Ee]xpiration: (.*)$/\1/p' <<<"$headers" | head -n 1)
    if [ -z "$expiry" ]; then
        fail "the credential has no expiry; agent panes must use the fine-grained agent token, not the owner's login"
    else
        local now expires days
        now=$(date -u +%s)
        expires=$(date -u -d "$expiry" +%s 2>/dev/null || echo 0)
        days=$(((expires - now) / 86400))
        if [ "$expires" -le "$now" ]; then
            fail "the token expired on $expiry"
        elif [ "$days" -lt 14 ]; then
            warn "the token expires in $days day(s) ($expiry); create a new one"
        else
            pass "fine-grained token, expires $expiry"
        fi
    fi

    local status
    status=$(gh api --include "repos/$repo/actions/permissions" 2>/dev/null | head -n 1 | tr -d '\r')
    if [[ $status == *" 403"* ]]; then
        pass "no Administration permission (the Administration-only endpoint answers 403)"
    else
        fail "the Administration-only endpoint answered '${status:-nothing}'; the token must not have Administration"
    fi

    local id name bypass count=0
    while IFS=$'\t' read -r id name; do
        [ -n "$id" ] || continue
        count=$((count + 1))
        bypass=$(gh api "repos/$repo/rulesets/$id" --jq '.current_user_can_bypass' 2>/dev/null)
        if [ "$bypass" = never ]; then
            pass "ruleset $name: current_user_can_bypass is never"
        else
            fail "ruleset $name: current_user_can_bypass is '${bypass:-unknown}', expected never"
        fi
    done < <(gh api "repos/$repo/rulesets" --jq '.[] | "\(.id)\t\(.name)"' 2>/dev/null)
    [ "$count" -ge 2 ] || fail "expected the main and release-tags rulesets, found $count"

    if [ -n "$pr" ]; then
        local state head consent
        state=$(gh api "repos/$repo/pulls/$pr" --jq .mergeable_state 2>/dev/null)
        head=$(gh api "repos/$repo/pulls/$pr" --jq .head.sha 2>/dev/null)
        consent=$(gh api "repos/$repo/commits/$head/check-runs?check_name=owner-consent" \
            --jq '[.check_runs[].conclusion] | first // "absent"' 2>/dev/null)
        if [ "$consent" != success ] && [ "$state" = blocked ]; then
            pass "pull request #$pr is blocked while owner-consent is $consent on ${head:0:12}"
        elif [ "$consent" = success ]; then
            warn "pull request #$pr: owner-consent is success on ${head:0:12} (the owner approved it); mergeable_state $state"
        else
            fail "pull request #$pr: mergeable_state is '$state' while owner-consent is $consent"
        fi
    fi

    echo
    echo "Confirm the token's grants at https://github.com/settings/personal-access-tokens (no API lists them):"
    echo "  repository access: only $repo; expiry at most 90 days;"
    echo "  Contents read/write, Pull requests read/write, Issues read/write, Actions read, Checks read, Metadata read;"
    echo "  no Administration, Deployments, Commit statuses, Workflows, Environments, Secrets or Variables."
    if [ -t 0 ]; then
        local answer
        read -r -p "Do the grants match exactly? [y/N] " answer
        if [ "$answer" = y ] || [ "$answer" = Y ]; then
            pass "the owner confirmed the token's grants"
        else
            fail "the owner did not confirm the token's grants"
        fi
    else
        fail "the token's grants need the owner's confirmation; run this in a terminal"
    fi
}

verify_owner() {
    check_login

    local environment
    environment=$(gh api "repos/$repo/environments/owner-review" 2>/dev/null)
    if [ -z "$environment" ]; then
        fail "the owner-review environment does not exist"
    else
        local reviewers admins
        reviewers=$(jq -r '[.protection_rules[]? | select(.type == "required_reviewers") | .reviewers[].reviewer.login] | join(",")' <<<"$environment")
        admins=$(jq -r '.can_admins_bypass' <<<"$environment")
        if [[ ",$reviewers," == *",$owner,"* ]]; then
            pass "owner-review requires $owner"
        else
            fail "owner-review reviewers are '$reviewers', expected $owner"
        fi
        if [ "$admins" = false ]; then
            pass "owner-review: administrators cannot bypass"
        else
            fail "owner-review: can_admins_bypass is $admins"
        fi
    fi

    local id name ruleset
    while IFS=$'\t' read -r id name; do
        [ -n "$id" ] || continue
        ruleset=$(gh api "repos/$repo/rulesets/$id")
        if [ "$(jq '.bypass_actors | length' <<<"$ruleset")" = 0 ]; then
            pass "ruleset $name has no bypass actors"
        else
            fail "ruleset $name has bypass actors"
        fi
        if [ "$name" = main ]; then
            local strict checks
            strict=$(jq -r '[.rules[] | select(.type == "required_status_checks") | .parameters.strict_required_status_checks_policy][0]' <<<"$ruleset")
            checks=$(jq -r '[.rules[] | select(.type == "required_status_checks") | .parameters.required_status_checks[].context] | join(" ")' <<<"$ruleset")
            if [ "$strict" = true ]; then
                pass "main: required checks are strict"
            else
                fail "main: strict required checks are '$strict'"
            fi
            if [[ " $checks " == *" owner-consent "* ]]; then
                pass "main requires: $checks"
            else
                fail "main does not require owner-consent (requires: $checks)"
            fi
        fi
    done < <(gh api "repos/$repo/rulesets" --jq '.[] | "\(.id)\t\(.name)"')

    local policy
    policy=$(gh api "repos/$repo/actions/permissions/fork-pr-contributor-approval" --jq .approval_policy 2>/dev/null)
    if [ "$policy" = all_external_contributors ]; then
        pass "workflow runs from all external contributors need approval"
    else
        fail "fork workflow approval policy is '${policy:-unknown}', expected all_external_contributors"
    fi
}

case "$mode" in
    agent) verify_agent ;;
    owner) verify_owner ;;
    *)
        echo "usage: $0 agent [--repo OWNER/NAME] [--pr NUMBER] | owner [--repo OWNER/NAME]" >&2
        exit 2
        ;;
esac

echo
if [ "$failures" -gt 0 ]; then
    echo "verify-identity: $failures check(s) failed"
    exit 1
fi
echo "verify-identity: every check passed"
