#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# The agent guard's policy: one implementation, called by the runtime adapters (claude-hook.sh, agy-hook.sh).
#
# Usage:
#   check-command.sh --cwd DIR --command TEXT   a shell command an agent is about to run
#   check-command.sh --cwd DIR --path PATH      a file an agent's edit tool is about to write
# Exit 0: allowed. Exit 1: denied, with one line "agent-guard: denied [<rule>] <reason>" on stdout. Exit 2: usage error.
#
# This is a text-level safety net against accidents, not a security boundary: an interpreter, a script or an absolute
# path to another tool can bypass it. The controls that hold are the server-side rulesets, the agent token's missing
# permissions and the read-only MacShot tree (docs/dev/agent-workflow.md).
#
# Commands are split on shell separators (; && || | & newlines, $( and backticks, parentheses) and each segment is
# read word by word; quotes are dropped and $NAME / ${NAME} are expanded from this process's environment, never
# evaluated. Relative paths resolve against --cwd, which "cd DIR" segments and "git -C DIR" update.

set -uo pipefail

if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
    echo "agent-guard: denied [bash-version] the guard needs bash 4 or newer"
    exit 1
fi

cwd=""
command_text=""
path_text=""
mode=""
while [ $# -gt 0 ]; do
    case "$1" in
        --cwd) cwd=${2-}; shift 2 ;;
        --command) command_text=${2-}; mode="command"; shift 2 ;;
        --path) path_text=${2-}; mode="path"; shift 2 ;;
        *) echo "usage: $0 --cwd DIR (--command TEXT | --path PATH)" >&2; exit 2 ;;
    esac
done
if [ -z "$mode" ] || [ -z "$cwd" ]; then
    echo "usage: $0 --cwd DIR (--command TEXT | --path PATH)" >&2
    exit 2
fi

deny() {
    printf 'agent-guard: denied [%s] %s\n' "$1" "$2"
    exit 1
}

# Prints the absolute, normalised form of $2 taken relative to $1. "." and ".." are resolved lexically; symbolic links
# in the longest existing directory prefix are resolved.
resolve_path() {
    local base=$1 path=$2
    if [ "$path" = "~" ]; then
        path=$HOME
    elif [[ $path == \~/* ]]; then
        path=$HOME/${path#"~/"}
    fi
    [[ $path == /* ]] || path=$base/$path
    local -a parts=() out=()
    local part
    IFS=/ read -r -a parts <<<"$path"
    for part in "${parts[@]}"; do
        case "$part" in
            '' | .) ;;
            ..) [ ${#out[@]} -gt 0 ] && unset "out[$((${#out[@]} - 1))]" ;;
            *) out+=("$part") ;;
        esac
    done
    local normalised
    normalised=$(IFS=/; printf '/%s' "${out[*]}")
    local head=$normalised tail=""
    while [ "$head" != / ] && [ ! -d "$head" ]; do
        tail=/${head##*/}$tail
        head=${head%/*}
        [ -n "$head" ] || head=/
    done
    local real
    real=$(cd -P -- "$head" 2>/dev/null && pwd -P) || real=$head
    [ "$real" = / ] && real=""
    local result=$real$tail
    printf '%s\n' "${result:-/}"
}

# True when $1 equals $2 or lies inside it.
is_within() {
    [ "$1" = "$2" ] || [[ $1 == "$2"/* ]]
}

macshot_dir=""
if [ -n "${ARIADSHOT_MACSHOT_DIR:-}" ]; then
    macshot_dir=$(resolve_path / "$ARIADSHOT_MACSHOT_DIR")
fi

# Expands $NAME and ${NAME} from the environment and removes quotes; nothing is evaluated.
expand_word() {
    local word=$1 name value
    word=${word//\"/}
    word=${word//\'/}
    while [[ $word =~ \$\{([A-Za-z_][A-Za-z0-9_]*)\} || $word =~ \$([A-Za-z_][A-Za-z0-9_]*) ]]; do
        name=${BASH_REMATCH[1]}
        value=${!name-}
        word=${word/"${BASH_REMATCH[0]}"/$value}
    done
    printf '%s' "$word"
}

# ---- File paths (edit tools) -------------------------------------------------------------------------------------

check_path() {
    local target
    target=$(resolve_path "$cwd" "$1")
    if [[ /$target/ == */.git/* ]]; then
        deny git-dir "writing inside .git ($target) is not allowed; git manages that directory"
    fi
    if [ -n "$macshot_dir" ] && is_within "$target" "$macshot_dir"; then
        deny macshot-readonly "the MacShot reference tree ($macshot_dir) is read-only"
    fi
}

# ---- git ----------------------------------------------------------------------------------------------------------

current_branch() {
    git -C "$1" symbolic-ref --quiet --short HEAD 2>/dev/null
}

is_local_tag() {
    [ -n "$2" ] && git -C "$1" show-ref --verify --quiet "refs/tags/$2" 2>/dev/null
}

check_git_push() {
    local dir=$1
    shift
    local -a positional=()
    local force=0 lease=0 delete=0 word
    while [ $# -gt 0 ]; do
        word=$1
        shift
        case "$word" in
            --all) deny push-all "git push --all pushes every branch, including main" ;;
            --mirror) deny push-mirror "git push --mirror rewrites every remote ref" ;;
            --tags | --follow-tags) deny push-tags "release tags are created by the owner's release script only" ;;
            --force-with-lease | --force-with-lease=*) lease=1 ;;
            --force | --force-if-includes) [ "$word" = --force ] && force=1 ;;
            --delete) delete=1 ;;
            --repo=* | --receive-pack=* | --exec=* | --push-option=* | --recurse-submodules=* | --signed=*) ;;
            --repo | --receive-pack | --exec | --push-option | -o) shift ;;
            --) positional+=("$@"); break ;;
            --*) ;;
            -*)
                [[ $word == *f* ]] && force=1
                [[ $word == *d* ]] && delete=1
                ;;
            *) positional+=("$word") ;;
        esac
    done
    if [ "$force" = 1 ] && [ "$lease" = 0 ]; then
        deny force-push "force pushes need --force-with-lease on your own branch"
    fi
    local branch
    branch=$(current_branch "$dir")
    if [ ${#positional[@]} -le 1 ]; then
        if [ "$branch" = main ] || [ "$branch" = master ]; then
            deny push-to-main "the current branch is $branch; changes reach main only through a pull request"
        fi
        return
    fi
    local refspec source destination
    for refspec in "${positional[@]:1}"; do
        if [[ $refspec == +* ]]; then
            [ "$lease" = 1 ] || deny force-push "a '+' refspec is a force push; use --force-with-lease on your own branch"
            refspec=${refspec#+}
        fi
        if [[ $refspec == *:* ]]; then
            source=${refspec%%:*}
            destination=${refspec#*:}
        else
            source=$refspec
            destination=$refspec
            [ "$delete" = 1 ] && source=""
        fi
        [ "$source" = HEAD ] && [[ $refspec != *:* ]] && destination=$branch
        if [[ $destination == refs/tags/* ]] || [[ $source == refs/tags/* ]] || is_local_tag "$dir" "$source"; then
            deny push-tags "release tags are created by the owner's release script only"
        fi
        destination=${destination#refs/heads/}
        if [ "$destination" = main ] || [ "$destination" = master ]; then
            deny push-to-main "pushing to $destination is not allowed; changes reach main only through a pull request"
        fi
    done
}

check_git() {
    local dir=$cwd word
    while [ $# -gt 0 ]; do
        word=$1
        case "$word" in
            -C) dir=$(resolve_path "$dir" "${2-}"); shift 2 ;;
            -c | --git-dir | --work-tree | --namespace | --exec-path | --config-env) shift 2 ;;
            -*) shift ;;
            *) break ;;
        esac
    done
    [ $# -gt 0 ] || return 0
    local subcommand=$1
    shift
    local args=" $* "
    case "$subcommand" in
        push) check_git_push "$dir" "$@" ;;
        reset)
            [[ $args == *" --hard "* ]] && deny reset-hard "git reset --hard discards work; use a new commit or git stash push"
            ;;
        clean)
            local word flags=""
            for word in "$@"; do
                case "$word" in
                    --force) flags+=f ;;
                    -*) [[ $word != --* ]] && flags+=${word#-} ;;
                esac
            done
            if [[ $flags == *f* ]] && [[ $flags == *[xXd]* ]]; then
                deny clean-force "git clean with -f and -d or -x deletes untracked and ignored work"
            fi
            ;;
        worktree)
            if [ "${1-}" = remove ] && [[ $args == *" --force "* || $args == *" -f "* ]]; then
                deny worktree-remove-force "remove worktrees with scripts/worktree.sh remove, which never forces"
            fi
            ;;
        filter-branch | filter-repo) deny history-rewrite "git $subcommand rewrites history" ;;
        update-ref)
            [[ $args == *" -d "* ]] && deny update-ref-delete "git update-ref -d deletes a ref"
            ;;
    esac
}

# ---- gh and the GitHub API ----------------------------------------------------------------------------------------

readonly SENSITIVE_API='/merge($|[/?])|/merges($|[/?])|/releases|/git/refs|/git/tags|/rulesets|/protection|/environments|/deployments|/pending_deployments|/collaborators|/invitations|/hooks|/keys|/secrets|/variables|/actions/permissions|/private-vulnerability-reporting|/vulnerability-alerts|^/?repos/[^/]+/[^/]+/?$'
readonly SENSITIVE_MUTATION='mergePullRequest|enablePullRequestAutoMerge|createRef|updateRef|deleteRef|updateRefs|Release|Ruleset|BranchProtection|updateRepository|deleteRepository|Environment|Deployment|Collaborator'

check_gh_api() {
    local method="" implied_post=0 endpoint="" word text=" $* "
    while [ $# -gt 0 ]; do
        word=$1
        shift
        case "$word" in
            -X | --method) method=${1-}; shift ;;
            -X*) method=${word#-X} ;;
            --method=*) method=${word#--method=} ;;
            -f | -F | --field | --raw-field | --input) implied_post=1; shift ;;
            --field=* | --raw-field=* | --input=*) implied_post=1 ;;
            -H | --header | -q | --jq | -t | --template | --hostname | -p | --preview | --cache) shift ;;
            -*) ;;
            *) [ -z "$endpoint" ] && endpoint=$word ;;
        esac
    done
    if [ -z "$method" ]; then
        method=GET
        [ "$implied_post" = 1 ] && method=POST
    fi
    method=$(printf '%s' "$method" | tr '[:lower:]' '[:upper:]')
    [ "$method" = GET ] || [ "$method" = HEAD ] && return 0
    if [ "$endpoint" = graphql ]; then
        if [[ $text =~ $SENSITIVE_MUTATION ]]; then
            deny gh-api-write "this GraphQL mutation merges, tags, releases or changes settings; the owner does that"
        fi
        return 0
    fi
    if [[ $endpoint =~ $SENSITIVE_API ]]; then
        deny gh-api-write "$method $endpoint merges, releases, tags or changes repository settings; the owner does that"
    fi
}

check_gh() {
    local group=${1-} action=${2-}
    case "$group" in
        pr) [ "$action" = merge ] && deny gh-merge "agents never merge; the owner merges after approving the owner gate" ;;
        release)
            case "$action" in
                list | view | '') ;;
                *) deny gh-release "releases are published by the owner" ;;
            esac
            ;;
        repo)
            case "$action" in
                edit | delete | rename | archive | unarchive) deny gh-repo-settings "repository settings belong to the owner" ;;
            esac
            ;;
        secret | variable) deny gh-secrets "secrets and variables belong to the owner" ;;
        ruleset)
            case "$action" in
                list | view | check | '') ;;
                *) deny gh-ruleset "rulesets belong to the owner" ;;
            esac
            ;;
        auth)
            case "$action" in
                status | '') ;;
                *) deny gh-auth "gh auth $action touches credentials; agents use the pane's agent token" ;;
            esac
            ;;
        api) shift; check_gh_api "$@" ;;
    esac
}

check_http_client() {
    local tool=$1 text=" ${*:2} "
    [[ $text == *api.github.com* ]] || return 0
    local write=0
    case "$tool" in
        curl)
            if [[ $text =~ \ (-X|--request)\ *(POST|PUT|PATCH|DELETE) || $text =~ \ -X(POST|PUT|PATCH|DELETE) ||
                $text =~ \ (-d|--data[a-z-]*|-F|--form|--json|-T|--upload-file)[\ =] ]]; then
                write=1
            fi
            ;;
        wget)
            [[ $text =~ --method=?\ *(POST|PUT|PATCH|DELETE) || $text =~ --(post|body)-(data|file) ]] && write=1
            ;;
        http | https | xh)
            [[ $text =~ \ (POST|PUT|PATCH|DELETE)\  ]] && write=1
            ;;
    esac
    [ "$write" = 1 ] && deny github-api-write "writing to the GitHub API outside gh is not allowed"
    return 0
}

# ---- rm -----------------------------------------------------------------------------------------------------------

check_rm() {
    local recursive=0 word
    local -a targets=()
    while [ $# -gt 0 ]; do
        word=$1
        shift
        case "$word" in
            --) targets+=("$@"); break ;;
            --recursive) recursive=1 ;;
            --*) ;;
            -*) [[ $word == *[rR]* ]] && recursive=1 ;;
            *) targets+=("$word") ;;
        esac
    done
    local toplevel="" common=""
    toplevel=$(git -C "$cwd" rev-parse --show-toplevel 2>/dev/null) && toplevel=$(resolve_path / "$toplevel")
    common=$(git -C "$cwd" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) && common=$(resolve_path / "$common")
    local target resolved
    for target in "${targets[@]}"; do
        # A glob is judged by the directory it expands in.
        if [[ $target == *[\*\?\[]* ]]; then
            target=$(dirname -- "$target")
        fi
        resolved=$(resolve_path "$cwd" "$target")
        if [ -n "$macshot_dir" ] && { is_within "$resolved" "$macshot_dir" || is_within "$macshot_dir" "$resolved"; }; then
            deny rm-protected "removing $resolved would delete the read-only MacShot reference tree"
        fi
        [ "$recursive" = 1 ] || continue
        if [[ /$resolved/ == */.git/* ]]; then
            deny rm-protected "removing $resolved deletes git metadata"
        fi
        if [ -n "$toplevel" ] && is_within "$toplevel" "$resolved"; then
            deny rm-protected "removing $resolved deletes the repository checkout"
        fi
        if [ -n "$common" ] && { is_within "$common" "$resolved" || is_within "$resolved" "$common"; }; then
            deny rm-protected "removing $resolved deletes git metadata"
        fi
    done
}

# ---- Commands -----------------------------------------------------------------------------------------------------

check_segment() {
    local segment=$1
    local -a raw=() words=()
    read -r -a raw <<<"$segment" || true
    local word
    for word in "${raw[@]}"; do
        words+=("$(expand_word "$word")")
    done
    # Skip environment assignments and command wrappers.
    while [ ${#words[@]} -gt 0 ]; do
        case "${words[0]}" in
            [A-Za-z_]*=*) words=("${words[@]:1}") ;;
            env | command | exec | nohup | time | builtin | sudo | doas) words=("${words[@]:1}") ;;
            -*) [ ${#words[@]} -gt 0 ] && words=("${words[@]:1}") ;;
            *) break ;;
        esac
    done
    [ ${#words[@]} -gt 0 ] || return 0
    local program=${words[0]##*/}
    case "$program" in
        cd)
            if [ ${#words[@]} -gt 1 ]; then
                cwd=$(resolve_path "$cwd" "${words[1]}")
            else
                cwd=$HOME
            fi
            ;;
        git) check_git "${words[@]:1}" ;;
        gh) check_gh "${words[@]:1}" ;;
        rm) check_rm "${words[@]:1}" ;;
        curl | wget | http | https | xh) check_http_client "$program" "${words[@]:1}" ;;
    esac
}

check_command() {
    local text=$1
    # shellcheck disable=SC2016 # a literal "$(" in the command text
    text=${text//'$('/$'\n'}
    text=${text//'`'/$'\n'}
    text=${text//'&&'/$'\n'}
    text=${text//'||'/$'\n'}
    text=${text//';'/$'\n'}
    text=${text//'|'/$'\n'}
    text=${text//'&'/$'\n'}
    text=${text//'('/$'\n'}
    text=${text//')'/$'\n'}
    local segment
    while IFS= read -r segment; do
        check_segment "$segment"
    done <<<"$text"
}

if [ "$mode" = path ]; then
    check_path "$path_text"
else
    check_command "$command_text"
fi
exit 0
