#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Owner-run, idempotent configuration of the GitHub repository (docs/dev/agent-workflow.md, "What GitHub enforces").
# Run it with the owner's own gh login, outside agent panes; the agent token cannot change these settings. It applies:
#   - settings: squash merge only (pull request title, blank body), branches deleted on merge, no wiki or projects,
#     secret scanning with push protection, private vulnerability reporting, and approval of workflow runs for all
#     external contributors;
#   - the environment "owner-review": the owner as required reviewer, self-review allowed, administrator bypass off;
#   - the "main" ruleset: pull request with zero approvals and resolved conversations, squash only, linear history, no
#     deletion or force push, and strict required checks (the CI jobs plus owner-consent) pinned to the GitHub Actions
#     app; no bypass actors;
#   - the "release-tags" ruleset on refs/tags/v*: creation, update and deletion restricted; no bypass actors;
#   - an Actions event policy that permits pull_request_target for the owner gate's workflow file only, because GitHub
#     blocks that event in public repositories by default from 2026-11-02;
#   - labels.tsv and milestones.tsv from this directory.
# Then it prints the active configuration back.
#
# Usage: setup-repository.sh [--repo OWNER/NAME] [--without-macos-14] [--bootstrap] [--dry-run]
#   --without-macos-14  drop the macos-14 required check once GitHub has retired that runner image (2026-11-02)
#   --bootstrap         the one-time exception for the scaffold pull request (docs/dev/agent-workflow.md, "Bootstrap of
#                       the owner gate"): apply the main ruleset without the owner-consent check, everything else
#                       identical. The owner gate runs from the base branch's workflow, and the repository's first
#                       commit has none, so that pull request can never get owner-consent. Refused once the default
#                       branch has the owner gate workflow. Re-run without the flag right after merging it.
#   --dry-run           print every request instead of sending it; reads nothing from GitHub

set -euo pipefail

repo=bavanchun/AriadShot
dry_run=0
with_macos_14=1
bootstrap=0
while [ $# -gt 0 ]; do
    case "$1" in
        --repo) repo=$2; shift 2 ;;
        --without-macos-14) with_macos_14=0; shift ;;
        --bootstrap) bootstrap=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        *) echo "usage: $0 [--repo OWNER/NAME] [--without-macos-14] [--bootstrap] [--dry-run]" >&2; exit 2 ;;
    esac
done
owner=${repo%%/*}
here=$(cd -- "$(dirname -- "$0")" && pwd)
readonly GATE_POLICY_NAME=owner-gate-pull-request-target
readonly GATE_WORKFLOW=.github/workflows/owner-gate.yml

required_checks=(lint commit-policy linux-gcc linux-clang linux-qt-floor macos)
[ "$with_macos_14" = 1 ] && required_checks+=(macos-14)
[ "$bootstrap" = 0 ] && required_checks+=(owner-consent)

# api METHOD PATH [JSON]: sends a request (or prints it in a dry run).
api() {
    local method=$1 path=$2 body=${3-}
    if [ "$dry_run" = 1 ]; then
        echo "DRY RUN: $method /$path"
        [ -n "$body" ] && jq . <<<"$body"
        return 0
    fi
    if [ -n "$body" ]; then
        gh api --method "$method" "$path" --input - <<<"$body" >/dev/null
    else
        gh api --method "$method" "$path" >/dev/null
    fi
}

step() {
    echo
    echo "== $1"
}

bootstrap_notice() {
    cat >&2 <<'NOTICE'

################################################################################################################
#  BOOTSTRAP: the main ruleset does NOT require owner-consent.                                                 #
#                                                                                                              #
#  Until this script runs again without --bootstrap, a pull request with green CI can merge without the owner  #
#  gate, and the agent token could merge it. Use this only to squash-merge the scaffold pull request after its #
#  attended checks, keep agent panes idle meanwhile, and then immediately run:                                 #
#      scripts/github/setup-repository.sh                                                                      #
#      scripts/github/verify-identity.sh owner                                                                 #
#  verify-identity.sh owner fails until owner-consent is required again.                                       #
################################################################################################################
NOTICE
}

if [ "$dry_run" = 0 ]; then
    login=$(gh api user --jq .login)
    if [ "$login" != "$owner" ]; then
        echo "setup-repository: gh is logged in as '$login'; run this with the owner's ($owner) own login" >&2
        exit 1
    fi
    owner_id=$(gh api "users/$owner" --jq .id)
    default_branch=$(gh api "repos/$repo" --jq .default_branch)
else
    owner_id=0
    default_branch=main
fi

if [ "$bootstrap" = 1 ]; then
    # The exception exists only while the default branch has no owner gate workflow; fail closed on any other answer.
    if [ "$dry_run" = 0 ]; then
        if gate_lookup=$(gh api "repos/$repo/contents/$GATE_WORKFLOW?ref=$default_branch" --jq .path 2>&1); then
            echo "setup-repository: $default_branch already has $GATE_WORKFLOW; the bootstrap exception is over," \
                "run this without --bootstrap" >&2
            exit 1
        elif [[ $gate_lookup != *"HTTP 404"* ]]; then
            echo "setup-repository: cannot tell whether $default_branch has $GATE_WORKFLOW: $gate_lookup" >&2
            exit 1
        fi
    fi
    bootstrap_notice
fi

step "Repository settings"
api PATCH "repos/$repo" "$(jq -cn '{
    allow_squash_merge: true, allow_merge_commit: false, allow_rebase_merge: false, allow_auto_merge: false,
    squash_merge_commit_title: "PR_TITLE", squash_merge_commit_message: "BLANK",
    delete_branch_on_merge: true, has_wiki: false, has_projects: false,
    security_and_analysis: {
        secret_scanning: {status: "enabled"},
        secret_scanning_push_protection: {status: "enabled"}
    }
}')"
api PUT "repos/$repo/private-vulnerability-reporting"
api PUT "repos/$repo/actions/permissions/fork-pr-contributor-approval" \
    '{"approval_policy":"all_external_contributors"}'

step "Actions event policy for the owner gate"
# GitHub blocks pull_request_target in public repositories by default from 2026-11-02, unless an applicable event
# policy permits it. This policy applies only to the owner gate's workflow file and permits only that event; the gate
# checks out nothing and runs no pull request code.
gate_policy=$(jq -cn --arg name "$GATE_POLICY_NAME" --arg path "$GATE_WORKFLOW" '{
    name: $name, enforcement: "active",
    conditions: {workflow_path: {include: [$path], exclude: []}},
    rules: [{type: "restrict_action_events", parameters: {allowed_events: ["pull_request_target"]}}]
}')
policy_id=""
if [ "$dry_run" = 0 ]; then
    policy_id=$(gh api "repos/$repo/actions/policies" |
        jq -r --arg name "$GATE_POLICY_NAME" '[.. | objects | select(.name? == $name) | .id] | first // empty')
fi
if [ -n "$policy_id" ]; then
    api PUT "repos/$repo/actions/policies/$policy_id" "$gate_policy"
else
    api POST "repos/$repo/actions/policies" "$gate_policy"
fi

step "Environment owner-review"
api PUT "repos/$repo/environments/owner-review" "$(jq -cn --argjson id "$owner_id" '{
    wait_timer: 0, prevent_self_review: false, can_admins_bypass: false,
    reviewers: [{type: "User", id: $id}], deployment_branch_policy: null
}')"

step "GitHub Actions app"
# Required checks are pinned to the app that creates them. The ID is read from an existing check run on the default
# branch, else from the app's public record; it is never hard-coded.
app_id=""
if [ "$dry_run" = 0 ]; then
    app_id=$(gh api "repos/$repo/commits/$default_branch/check-runs" \
        --jq '[.check_runs[] | select(.app.slug == "github-actions") | .app.id][0] // empty' 2>/dev/null || true)
    if [ -n "$app_id" ]; then
        echo "GitHub Actions app ID $app_id, from a check run on $default_branch"
    else
        app_id=$(gh api apps/github-actions --jq .id)
        echo "GitHub Actions app ID $app_id, from the app's public record (no check run on $default_branch yet)"
    fi
else
    app_id=0
fi

checks_json=$(printf '%s\n' "${required_checks[@]}" |
    jq -R --argjson app "$app_id" '{context: ., integration_id: $app}' | jq -cs .)

main_ruleset=$(jq -cn --argjson checks "$checks_json" '{
    name: "main", target: "branch", enforcement: "active", bypass_actors: [],
    conditions: {ref_name: {include: ["~DEFAULT_BRANCH"], exclude: []}},
    rules: [
        {type: "deletion"},
        {type: "non_fast_forward"},
        {type: "required_linear_history"},
        {type: "pull_request", parameters: {
            required_approving_review_count: 0, dismiss_stale_reviews_on_push: false,
            require_code_owner_review: false, require_last_push_approval: false,
            required_review_thread_resolution: true, allowed_merge_methods: ["squash"]}},
        {type: "required_status_checks", parameters: {
            strict_required_status_checks_policy: true, do_not_enforce_on_create: false,
            required_status_checks: $checks}}
    ]
}')
tags_ruleset=$(jq -cn '{
    name: "release-tags", target: "tag", enforcement: "active", bypass_actors: [],
    conditions: {ref_name: {include: ["refs/tags/v*"], exclude: []}},
    rules: [{type: "creation"}, {type: "update"}, {type: "deletion"}]
}')

apply_ruleset() {
    local name=$1 body=$2 id=""
    if [ "$dry_run" = 0 ]; then
        id=$(gh api "repos/$repo/rulesets" --jq ".[] | select(.name == \"$name\") | .id")
    fi
    if [ -n "$id" ]; then
        api PUT "repos/$repo/rulesets/$id" "$body"
    else
        api POST "repos/$repo/rulesets" "$body"
    fi
}

step "Ruleset main (required checks: ${required_checks[*]})"
apply_ruleset main "$main_ruleset"
[ "$bootstrap" = 1 ] && echo "BOOTSTRAP: owner-consent is not required; re-run without --bootstrap after the merge"
step "Ruleset release-tags"
apply_ruleset release-tags "$tags_ruleset"

step "Labels"
while IFS=$'\t' read -r name colour description; do
    [[ -z $name || $name == \#* ]] && continue
    if [ "$dry_run" = 1 ]; then
        echo "DRY RUN: label $name ($colour): $description"
    else
        gh label create "$name" --repo "$repo" --color "$colour" --description "$description" --force >/dev/null
    fi
done <"$here/labels.tsv"
echo "labels applied"

step "Milestones"
existing='[]'
[ "$dry_run" = 0 ] && existing=$(gh api --paginate "repos/$repo/milestones?state=all&per_page=100" | jq -cs 'add // []')
while IFS=$'\t' read -r title description; do
    [[ -z $title || $title == \#* ]] && continue
    number=$(jq -r --arg title "$title" '.[] | select(.title == $title) | .number' <<<"$existing")
    body=$(jq -cn --arg title "$title" --arg description "$description" '{title: $title, description: $description}')
    if [ -n "$number" ]; then
        api PATCH "repos/$repo/milestones/$number" "$body"
    else
        api POST "repos/$repo/milestones" "$body"
    fi
done <"$here/milestones.tsv"
echo "milestones applied"

if [ "$dry_run" = 1 ]; then
    [ "$bootstrap" = 1 ] && bootstrap_notice
    exit 0
fi

step "Active configuration"
gh api "repos/$repo" --jq '{allow_squash_merge, allow_merge_commit, allow_rebase_merge, squash_merge_commit_title,
    squash_merge_commit_message, delete_branch_on_merge, has_wiki, has_projects,
    secret_scanning: .security_and_analysis.secret_scanning.status,
    push_protection: .security_and_analysis.secret_scanning_push_protection.status}'
gh api "repos/$repo/actions/permissions/fork-pr-contributor-approval"
policy_id=$(gh api "repos/$repo/actions/policies" |
    jq -r --arg name "$GATE_POLICY_NAME" '[.. | objects | select(.name? == $name) | .id] | first // empty')
if [ -n "$policy_id" ]; then
    gh api "repos/$repo/actions/policies/$policy_id" \
        --jq '{name, enforcement, conditions, rules}'
else
    echo "no owner-gate event policy"
fi
gh api "repos/$repo/environments/owner-review" --jq '{name, can_admins_bypass,
    reviewers: [.protection_rules[]? | select(.type == "required_reviewers") | .reviewers[].reviewer.login],
    prevent_self_review: [.protection_rules[]? | select(.type == "required_reviewers") | .prevent_self_review][0]}'
for id in $(gh api "repos/$repo/rulesets" --jq '.[].id'); do
    gh api "repos/$repo/rulesets/$id" --jq '{name, target, enforcement, bypass_actors,
        conditions: .conditions.ref_name.include, rules: [.rules[] | .type],
        strict: ([.rules[] | select(.type == "required_status_checks") | .parameters.strict_required_status_checks_policy][0]),
        checks: ([.rules[] | select(.type == "required_status_checks") | .parameters.required_status_checks[] | "\(.context)@\(.integration_id)"])}'
done
if [ "$bootstrap" = 1 ]; then
    bootstrap_notice
fi
