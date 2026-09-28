#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Include rules per module directory (docs/spec/02-modules-and-interfaces.md §2, rule 4). The configure-time link check
# sees only targets; this check sees #include lines, including a header of an allowed module that pulls in a forbidden
# one (reported "through" that header).
#
#   core      QtCore only; project headers from core/
#   platform  QtCore and QtGui; project headers from core/ and platform/
#   render    QtCore and QtGui; project headers from core/ and render/
#   ui        QtCore and QtGui; project headers from core/, render/, platform/ and ui/
#
# None of these may include Qt private or QPA headers or platform headers (Wayland, X11, Apple frameworks, PipeWire,
# D-Bus, GLib, FFmpeg, ...). Outside src/backends/, no code may look at the desktop's identity through environment
# variables such as XDG_CURRENT_DESKTOP: code asks the capability registry instead.
#
# Usage: check-architecture.sh [--root DIR]   (default: the repository root; DIR must contain src/)
# Qt headers are located through $ARIADSHOT_QT_HEADERS (and $ARIADSHOT_QT_LIBS for framework builds), else through
# qtpaths6, qtpaths, qmake6 or qmake. Violations print "ariadshot-arch: <file>:<line>: <rule>" and exit 1.

set -uo pipefail

if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
    echo "check-architecture: bash 4 or newer is required" >&2
    exit 2
fi

root=""
while [ $# -gt 0 ]; do
    case "$1" in
        --root) root=${2:-}; shift 2 ;;
        *) echo "usage: $0 [--root DIR]" >&2; exit 2 ;;
    esac
done
if [ -z "$root" ]; then
    root=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "check-architecture: not in a git checkout; pass --root" >&2; exit 2; }
fi
if [ ! -d "$root/src" ]; then
    echo "check-architecture: $root has no src/ directory" >&2
    exit 2
fi

qt_headers=${ARIADSHOT_QT_HEADERS:-}
qt_libs=${ARIADSHOT_QT_LIBS:-}
if [ -z "$qt_headers" ]; then
    for tool in qtpaths6 qtpaths qmake6 qmake; do
        if command -v "$tool" >/dev/null 2>&1; then
            query=-query
            [[ $tool == qtpaths* ]] && query=--query
            qt_headers=$("$tool" "$query" QT_INSTALL_HEADERS 2>/dev/null)
            qt_libs=$("$tool" "$query" QT_INSTALL_LIBS 2>/dev/null)
            [ -n "$qt_headers" ] && break
        fi
    done
fi
# Framework builds (macOS) keep the headers in <libs>/<Module>.framework/Headers.
if { [ -z "$qt_headers" ] || [ ! -d "$qt_headers" ]; } && { [ -z "$qt_libs" ] || [ ! -d "$qt_libs/QtCore.framework" ]; }; then
    echo "check-architecture: cannot locate the Qt headers; set ARIADSHOT_QT_HEADERS or put qtpaths or qmake on PATH" >&2
    exit 2
fi

readonly QT_MODULES="QtCore QtGui QtWidgets QtQuick QtQml QtQuickWidgets QtOpenGL QtOpenGLWidgets QtDBus QtNetwork \
QtNetworkAuth QtWaylandClient QtMultimedia QtSvg QtSvgWidgets QtTest QtConcurrent QtXml QtShaderTools QtPrintSupport"
readonly PLATFORM_HEADERS='^(wayland-|xcb/|X11/|xkbcommon/|Cocoa/|AppKit/|Foundation/|CoreFoundation/|CoreGraphics/|CoreVideo/|IOSurface/|ScreenCaptureKit/|Carbon/|ApplicationServices/|IOKit/|AVFoundation/|Vision/|pipewire/|spa/|gio/|glib|dbus/|LayerShellQt/|libav[a-z]+/|libsw[a-z]+/|va/|drm|xf86drm|gbm\.h|EGL/|GL/|vulkan/|windows\.h)'
readonly DESKTOP_NAMES='XDG_CURRENT_DESKTOP|XDG_SESSION_DESKTOP|DESKTOP_SESSION|HYPRLAND_INSTANCE_SIGNATURE|SWAYSOCK|KDE_FULL_SESSION|KDE_SESSION_VERSION|GNOME_DESKTOP_SESSION_ID'
readonly INCLUDE_LINE='^[[:space:]]*#[[:space:]]*(include|import)[[:space:]]*([<"])([^>"]+)[>"]'
readonly PROJECT_DIRS='core|render|ui|platform|hosts|windows|backends|media|ml|net|app|cli'

declare -A allowed_qt=(
    [core]="QtCore"
    [platform]="QtCore QtGui"
    [render]="QtCore QtGui"
    [ui]="QtCore QtGui"
)
declare -A allowed_project=(
    [core]="core"
    [platform]="core platform"
    [render]="core render"
    [ui]="core render platform ui"
)
declare -A module_cache=()
violations=0
checked=0

violation() {
    printf 'ariadshot-arch: %s\n' "$1"
    violations=$((violations + 1))
}

# Prints the Qt module that provides an angle-bracket include, "private" or "qpa" for non-public API, or nothing.
qt_module_of() {
    local header=$1
    if [ -n "${module_cache[$header]+set}" ]; then
        printf '%s' "${module_cache[$header]}"
        return
    fi
    local module=""
    if [[ $header == qpa/* ]]; then
        module=qpa
    elif [[ $header == private/* || $header == */private/* || $header == *_p.h ]]; then
        module=private
    elif [[ $header =~ ^(Qt[A-Za-z]+)/ ]]; then
        module=${BASH_REMATCH[1]}
    elif [[ $header =~ ^Q[A-Za-z0-9]+$ || $header =~ ^q[a-z0-9_]+\.h$ ]]; then
        local candidate
        for candidate in $QT_MODULES; do
            if [ -e "$qt_headers/$candidate/$header" ] ||
                { [ -n "$qt_libs" ] && [ -e "$qt_libs/$candidate.framework/Headers/$header" ]; }; then
                module=$candidate
                break
            fi
        done
    fi
    module_cache[$header]=$module
    printf '%s' "$module"
}

# check_includes <module> <file> <reported file> <line prefix> <through> <visited...>
# Checks the #include lines of <file> against <module>'s rules and follows allowed project headers.
check_includes() {
    local module=$1 file=$2 reported=$3 through=$4
    shift 4
    local -a visited=("$@")
    local number line kind header
    while IFS=: read -r number line; do
        [[ $line =~ $INCLUDE_LINE ]] || continue
        kind=${BASH_REMATCH[2]}
        header=${BASH_REMATCH[3]}
        local where="$reported:$number" suffix=""
        if [ -n "$through" ]; then
            where=$through
            suffix=" through ${file#"$root"/src/}"
        fi
        if [ "$kind" = '"' ] && [[ $header =~ ^($PROJECT_DIRS)/ ]]; then
            local dir=${BASH_REMATCH[1]}
            if [[ " ${allowed_project[$module]} " != *" $dir "* ]]; then
                violation "$where: $module may not include \"$header\"$suffix"
                continue
            fi
            local target="$root/src/$header"
            if [ -f "$target" ] && [[ " ${visited[*]} " != *" $target "* ]]; then
                check_includes "$module" "$target" "$reported" "${through:-$reported:$number}" "${visited[@]}" "$target"
            fi
            continue
        fi
        local qt_module
        qt_module=$(qt_module_of "$header")
        if [ "$qt_module" = private ] || [ "$qt_module" = qpa ]; then
            violation "$where: $module may not include <$header> (Qt $qt_module API)$suffix"
        elif [ -n "$qt_module" ]; then
            if [[ " ${allowed_qt[$module]} " != *" $qt_module "* ]]; then
                violation "$where: $module may not include <$header> ($qt_module)$suffix"
            fi
        elif [[ $header =~ $PLATFORM_HEADERS ]]; then
            violation "$where: $module may not include the platform header <$header>$suffix"
        fi
    done < <(grep -nE '^[[:space:]]*#[[:space:]]*(include|import)' "$file")
}

while IFS= read -r -d '' file; do
    checked=$((checked + 1))
    relative=${file#"$root"/}
    module_dir=${relative#src/}
    module_dir=${module_dir%%/*}
    if [ -n "${allowed_qt[$module_dir]+set}" ]; then
        check_includes "$module_dir" "$file" "$relative" "" "$file"
    fi
    if [ "$module_dir" != backends ]; then
        while IFS=: read -r number _; do
            violation "$relative:$number: desktop-name check outside src/backends/; ask the capability registry"
        done < <(grep -nE "$DESKTOP_NAMES" "$file")
    fi
done < <(find "$root/src" -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' -o -name '*.c' \
    -o -name '*.mm' -o -name '*.m' \) -print0 | sort -z)

if [ "$violations" -gt 0 ]; then
    echo "check-architecture: $violations violation(s) in $checked file(s)"
    exit 1
fi
echo "check-architecture: $checked file(s) checked, no violations"
