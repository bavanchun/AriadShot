// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <tl/expected.hpp>

namespace ariadshot {

template <typename T, typename E> using Expected = tl::expected<T, E>;

} // namespace ariadshot
