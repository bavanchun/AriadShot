// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the censor blur's sigma rule follows macshot/Model/Annotation.swift:2253-2272@b4d4f3a (see
// PROVENANCE.md).

#include "render/effects/Blur.h"

#include "render/CanonicalImage.h"
#include "render/pixel/PixelSpan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace ariadshot::render {

namespace {

// The censor blur's sigma is at least this many pixels (macshot/Model/Annotation.swift:2256@b4d4f3a) ...
constexpr double kCensorBlurMinimumSigma = 10.0;
// ... and at least this fraction of the region's shorter side (macshot/Model/Annotation.swift:2256@b4d4f3a).
constexpr double kCensorBlurSigmaPerSide = 0.03;

// Larger sigmas are refused: the box radii would exceed any image and the index arithmetic assumes they fit an int.
constexpr double kLargestSigma = 1.0e6;

// Alpha, red, green and blue are blurred as four interleaved float channels.
constexpr std::size_t kChannels = 4;

using Scratch = std::unique_ptr<float[]>;

// Working memory that reports failure by returning null instead of throwing.
Scratch allocateScratch(std::size_t floatCount) { return Scratch(new (std::nothrow) float[floatCount]); }

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

// One box pass of the given radius over the interleaved pixels of `source`, with the ends repeated. The window sum is
// kept in double and updated by one pixel entering and one leaving, so a constant line stays exactly constant.
void boxPass(std::span<const float> source, std::span<float> target, int radius) {
    const int count = static_cast<int>(source.size() / kChannels);
    const double window = 2.0 * radius + 1.0;
    const auto at = [&source](int pixel, std::size_t channel) -> double {
        return source[toIndex(pixel) * kChannels + channel];
    };

    // The window of pixel 0 holds pixel 0 radius + 1 times, pixels 1 to radius, and the last pixel for any taps past
    // it.
    const int inside = std::min(radius, count - 1);
    std::array<double, kChannels> sum{};
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        sum[channel] = at(0, channel) * (radius + 1) + at(count - 1, channel) * (radius - inside);
        for (int pixel = 1; pixel <= inside; ++pixel) {
            sum[channel] += at(pixel, channel);
        }
    }
    for (int pixel = 0; pixel < count; ++pixel) {
        const int entering = std::min(pixel + radius + 1, count - 1);
        const int leaving = std::max(pixel - radius, 0);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            target[toIndex(pixel) * kChannels + channel] = static_cast<float>(sum[channel] / window);
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

void unpackPixel(std::uint32_t pixel, std::span<float, kChannels> channels) {
    channels[0] = static_cast<float>(pixel >> 24);
    channels[1] = static_cast<float>((pixel >> 16) & 0xffU);
    channels[2] = static_cast<float>((pixel >> 8) & 0xffU);
    channels[3] = static_cast<float>(pixel & 0xffU);
}

std::uint32_t packPremultiplied(std::span<const float, kChannels> channels) {
    std::array<std::uint32_t, kChannels> level{};
    for (std::size_t i = 0; i < kChannels; ++i) {
        level[i] = static_cast<std::uint32_t>(std::clamp(std::lround(channels[i]), 0L, 255L));
    }
    // Rounding both is monotonic, so a colour never exceeds alpha; the guard only absorbs float error.
    const std::uint32_t alpha = level[0];
    return (alpha << 24) | (std::min(level[1], alpha) << 16) | (std::min(level[2], alpha) << 8) |
           std::min(level[3], alpha);
}

} // namespace

double censorBlurSigma(QSize regionPixelSize) {
    const double shorterSide = std::min(regionPixelSize.width(), regionPixelSize.height());
    return std::max(kCensorBlurMinimumSigma, shorterSide * kCensorBlurSigmaPerSide);
}

std::array<int, 3> boxBlurWidths(double sigma) {
    constexpr int kBoxes = 3;
    if (!std::isfinite(sigma) || sigma <= 0.0) {
        return {1, 1, 1};
    }
    sigma = std::min(sigma, kLargestSigma);
    // The widths stay doubles until the end: their squares exceed an int for sigmas above about 13000.
    const double idealWidth = std::sqrt(12.0 * sigma * sigma / kBoxes + 1.0);
    double lower = std::floor(idealWidth);
    if (std::fmod(lower, 2.0) == 0.0) {
        lower -= 1.0;
    }
    const double idealLowerBoxes =
        (12.0 * sigma * sigma - kBoxes * lower * lower - 4.0 * kBoxes * lower - 3.0 * kBoxes) / (-4.0 * lower - 4.0);
    const int lowerBoxes = static_cast<int>(std::lround(idealLowerBoxes));
    const int lowerWidth = static_cast<int>(lower);
    std::array<int, 3> widths{};
    for (std::size_t box = 0; box < widths.size(); ++box) {
        widths[box] = std::cmp_less(box, lowerBoxes) ? lowerWidth : lowerWidth + 2;
    }
    return widths;
}

QImage gaussianBlur(const QImage& source, double sigma) {
    if (!(sigma >= 0.0) || sigma > kLargestSigma) {
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
    const std::size_t longestLine = std::max(width, height) * kChannels;
    const Scratch plane = allocateScratch(width * height * kChannels);
    const Scratch lineBuffer = allocateScratch(longestLine);
    const Scratch spareBuffer = allocateScratch(longestLine);
    if (!plane || !lineBuffer || !spareBuffer) {
        return {};
    }
    const std::array<int, 3> widths = boxBlurWidths(sigma);
    const std::array<int, 3> radii = {(widths[0] - 1) / 2, (widths[1] - 1) / 2, (widths[2] - 1) / 2};
    const auto pixelAt = [&plane, width](std::size_t x, std::size_t y) {
        return plane.get() + (y * width + x) * kChannels;
    };

    // Rows: load, blur, keep the result in the float plane.
    const std::span<float> rowLine(lineBuffer.get(), width * kChannels);
    const std::span<float> rowSpare(spareBuffer.get(), width * kChannels);
    for (std::size_t y = 0; y < height; ++y) {
        const std::span<const std::uint32_t> row = input.row(static_cast<int>(y));
        for (std::size_t x = 0; x < width; ++x) {
            unpackPixel(row[x], rowLine.subspan(x * kChannels).first<kChannels>());
        }
        threePasses(rowLine, rowSpare, radii);
        std::ranges::copy(rowSpare, pixelAt(0, y));
    }

    // Columns: gather from the plane, blur, put the result back.
    const std::span<float> columnLine(lineBuffer.get(), height * kChannels);
    const std::span<float> columnSpare(spareBuffer.get(), height * kChannels);
    for (std::size_t x = 0; x < width; ++x) {
        for (std::size_t y = 0; y < height; ++y) {
            std::copy_n(pixelAt(x, y), kChannels, columnLine.begin() + static_cast<std::ptrdiff_t>(y * kChannels));
        }
        threePasses(columnLine, columnSpare, radii);
        for (std::size_t y = 0; y < height; ++y) {
            std::copy_n(columnSpare.begin() + static_cast<std::ptrdiff_t>(y * kChannels), kChannels, pixelAt(x, y));
        }
    }

    for (std::size_t y = 0; y < height; ++y) {
        const std::span<std::uint32_t> row = output.row(static_cast<int>(y));
        for (std::size_t x = 0; x < width; ++x) {
            row[x] = packPremultiplied(std::span<const float, kChannels>(pixelAt(x, y), kChannels));
        }
    }
    return result;
}

} // namespace ariadshot::render
