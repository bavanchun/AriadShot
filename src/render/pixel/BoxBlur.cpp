// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/pixel/BoxBlur.h"

#include "render/CanonicalImage.h"
#include "render/pixel/FloatChannels.h"
#include "render/pixel/PixelSpan.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ariadshot::render {

namespace {

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

// One box pass of the given radius over the interleaved pixels of `source`, with the ends repeated. The window sum is
// kept in double and updated by one pixel entering and one leaving, so a constant line stays exactly constant. Indices
// are 64-bit, so a radius of any size cannot overflow them.
void boxPass(std::span<const float> source, std::span<float> target, int radius) {
    const auto count = static_cast<std::int64_t>(source.size() / kPixelChannels);
    const auto reach = static_cast<std::int64_t>(radius);
    const double window = 2.0 * radius + 1.0;
    const auto at = [&source](std::int64_t pixel, std::size_t channel) -> double {
        return source[static_cast<std::size_t>(pixel) * kPixelChannels + channel];
    };

    // The window of pixel 0 holds pixel 0 radius + 1 times, pixels 1 to radius, and the last pixel for any taps past
    // it.
    const std::int64_t inside = std::min(reach, count - 1);
    std::array<double, kPixelChannels> sum{};
    for (std::size_t channel = 0; channel < kPixelChannels; ++channel) {
        sum[channel] = at(0, channel) * (static_cast<double>(radius) + 1.0) +
                       at(count - 1, channel) * static_cast<double>(reach - inside);
        for (std::int64_t pixel = 1; pixel <= inside; ++pixel) {
            sum[channel] += at(pixel, channel);
        }
    }
    for (std::int64_t pixel = 0; pixel < count; ++pixel) {
        const std::int64_t entering = std::min(pixel + reach + 1, count - 1);
        const std::int64_t leaving = std::max<std::int64_t>(pixel - reach, 0);
        for (std::size_t channel = 0; channel < kPixelChannels; ++channel) {
            target[static_cast<std::size_t>(pixel) * kPixelChannels + channel] =
                static_cast<float>(sum[channel] / window);
            sum[channel] += at(entering, channel) - at(leaving, channel);
        }
    }
}

// The three passes over one line; `line` and `spare` are scratch, and the result is left in `spare`.
void threePasses(std::span<float> line, std::span<float> spare, const std::array<int, 3>& radii) {
    boxPass(line, spare, radii[0]);
    boxPass(spare, line, radii[1]);
    boxPass(line, spare, radii[2]);
}

} // namespace

QImage boxBlur(const QImage& source, const std::array<int, 3>& radii) {
    if (std::any_of(radii.begin(), radii.end(), [](int radius) { return radius < 0; })) {
        return {};
    }
    const ConstPixelSpan input(source);
    if (input.isEmpty()) {
        return {};
    }
    QImage result = makeCanonicalImage(source.size(), source.devicePixelRatio());
    const PixelSpan output(result);
    if (output.isEmpty()) {
        return {};
    }

    const std::size_t width = toIndex(input.width());
    const std::size_t height = toIndex(input.height());
    const std::size_t longestLine = std::max(width, height) * kPixelChannels;
    const FloatBuffer plane = allocateFloats(width * height * kPixelChannels);
    const FloatBuffer lineBuffer = allocateFloats(longestLine);
    const FloatBuffer spareBuffer = allocateFloats(longestLine);
    if (!plane || !lineBuffer || !spareBuffer) {
        return {};
    }
    const auto pixelAt = [&plane, width](std::size_t x, std::size_t y) {
        return plane.get() + (y * width + x) * kPixelChannels;
    };

    // Rows: load, blur, keep the result in the float plane.
    const std::span<float> rowLine(lineBuffer.get(), width * kPixelChannels);
    const std::span<float> rowSpare(spareBuffer.get(), width * kPixelChannels);
    for (std::size_t y = 0; y < height; ++y) {
        const std::span<const std::uint32_t> row = input.row(static_cast<std::int64_t>(y));
        for (std::size_t x = 0; x < width; ++x) {
            unpackPixel(row[x], rowLine.subspan(x * kPixelChannels).first<kPixelChannels>());
        }
        threePasses(rowLine, rowSpare, radii);
        std::copy(rowSpare.begin(), rowSpare.end(), pixelAt(0, y));
    }

    // Columns: gather from the plane, blur, put the result back.
    const std::span<float> columnLine(lineBuffer.get(), height * kPixelChannels);
    const std::span<float> columnSpare(spareBuffer.get(), height * kPixelChannels);
    for (std::size_t x = 0; x < width; ++x) {
        for (std::size_t y = 0; y < height; ++y) {
            std::copy_n(pixelAt(x, y), kPixelChannels,
                        columnLine.begin() + static_cast<std::ptrdiff_t>(y * kPixelChannels));
        }
        threePasses(columnLine, columnSpare, radii);
        for (std::size_t y = 0; y < height; ++y) {
            std::copy_n(columnSpare.begin() + static_cast<std::ptrdiff_t>(y * kPixelChannels), kPixelChannels,
                        pixelAt(x, y));
        }
    }

    for (std::size_t y = 0; y < height; ++y) {
        const std::span<std::uint32_t> row = output.row(static_cast<std::int64_t>(y));
        for (std::size_t x = 0; x < width; ++x) {
            row[x] = packPremultiplied(std::span<const float, kPixelChannels>(pixelAt(x, y), kPixelChannels));
        }
    }
    return result;
}

} // namespace ariadshot::render
