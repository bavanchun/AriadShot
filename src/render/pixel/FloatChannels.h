// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>

namespace ariadshot::render {

// The working form of the pixel operations that average: the four channels of a premultiplied pixel (alpha, red, green,
// blue) as interleaved floats. It is shared by the operations of this module and is not an interface of its own.
inline constexpr std::size_t kPixelChannels = 4;

using FloatBuffer = std::unique_ptr<float[]>;

// Working memory that reports failure by returning null instead of throwing. The extents of an image are below 2^31, so
// a plane of them has fewer than 2^64 / 4 floats and its size in bytes cannot overflow std::size_t.
[[nodiscard]] inline FloatBuffer allocateFloats(std::size_t count) {
    return FloatBuffer(new (std::nothrow) float[count]);
}

inline void unpackPixel(std::uint32_t pixel, std::span<float, kPixelChannels> channels) {
    channels[0] = static_cast<float>(pixel >> 24);
    channels[1] = static_cast<float>((pixel >> 16) & 0xffU);
    channels[2] = static_cast<float>((pixel >> 8) & 0xffU);
    channels[3] = static_cast<float>(pixel & 0xffU);
}

// The pixel of the four channels, each rounded to nearest (halves up) and limited to [0, 255]; a colour channel never
// exceeds alpha.
[[nodiscard]] inline std::uint32_t packPremultiplied(std::span<const float, kPixelChannels> channels) {
    std::array<std::uint32_t, kPixelChannels> level{};
    for (std::size_t i = 0; i < kPixelChannels; ++i) {
        level[i] = static_cast<std::uint32_t>(std::clamp(std::lround(channels[i]), 0L, 255L));
    }
    // Rounding both is monotonic, so a colour never exceeds alpha; the guard only absorbs float error.
    const std::uint32_t alpha = level[0];
    return (alpha << 24) | (std::min(level[1], alpha) << 16) | (std::min(level[2], alpha) << 8) |
           std::min(level[3], alpha);
}

} // namespace ariadshot::render
