// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/ViewObject.h"

#include <QAccessibleInterface>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <utility>

// The child lookups of an accessible container, shared by the interface of a root and the one of a view. `views` is a
// vector of view objects, back to front, as pointers or smart pointers; a child is the interface of one of them.
namespace ariadshot::ui::detail {

// The front-most child under a screen point.
template <typename Views> QAccessibleInterface* accessibleChildAt(const Views& views, int x, int y) {
    for (const auto& view : std::views::reverse(views)) {
        if (std::to_address(view)->screenRect().contains(x, y)) {
            return std::to_address(view)->accessible();
        }
    }
    return nullptr;
}

template <typename Views> QAccessibleInterface* accessibleChild(const Views& views, int index) {
    const bool held = index >= 0 && std::cmp_less(index, views.size());
    return held ? std::to_address(views[static_cast<std::size_t>(index)])->accessible() : nullptr;
}

template <typename Views> int accessibleIndexOfChild(const Views& views, const QAccessibleInterface* child) {
    const auto found =
        std::ranges::find_if(views, [child](const auto& view) { return std::to_address(view)->accessible() == child; });
    return found != views.end() ? static_cast<int>(found - views.begin()) : -1;
}

} // namespace ariadshot::ui::detail
