// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the step sizes follow macshot/Model/Annotation.swift:1953-2020@b4d4f3a (see PROVENANCE.md).

#include "render/effects/Pixelate.h"

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

namespace ariadshot::render {

namespace {

// The region is reduced by this factor in the first step (macshot/Model/Annotation.swift:1990-1992@b4d4f3a) ...
constexpr int kPixelBlock = 8;
// ... reduced by this factor in the second step (macshot/Model/Annotation.swift:2002-2003@b4d4f3a) ...
constexpr int kSecondStepDivisor = 2;
// ... and enlarged to this many pixels per point of the region's rectangle
// (macshot/Model/Annotation.swift:2010-2011@b4d4f3a).
constexpr double kOutputPixelsPerPoint = 2.0;
constexpr qreal kOutputDevicePixelRatio = kOutputPixelsPerPoint;

// The largest extent handed to the canvas factory, which then refuses the allocation.
constexpr double kLargestExtent = 2147483647.0;

constexpr std::size_t kChannels = 4;

using Scratch = std::unique_ptr<float[]>;

Scratch allocateScratch(std::size_t floatCount) { return Scratch(new (std::nothrow) float[floatCount]); }

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

int outputExtent(double points) {
    return std::max(1, static_cast<int>(std::min(points * kOutputPixelsPerPoint, kLargestExtent)));
}

// Calls visit(sourceIndex, weight) for each of the sourceCount pixels that destination pixel `index` of
// destinationCount covers when a line is resampled by area averaging; the weights are the covered fractions of the
// destination pixel and add up to 1. Positions are exact integers in units of 1 / destinationCount source pixels.
template <typename Visit> void forEachCoveredSource(int index, int sourceCount, int destinationCount, Visit visit) {
    const std::int64_t begin = static_cast<std::int64_t>(index) * sourceCount;
    const std::int64_t end = begin + sourceCount;
    for (std::int64_t source = begin / destinationCount; source <= (end - 1) / destinationCount; ++source) {
        const std::int64_t overlap =
            std::min(end, (source + 1) * destinationCount) - std::max(begin, source * destinationCount);
        visit(static_cast<int>(source), static_cast<double>(overlap) / sourceCount);
    }
}

std::uint32_t packPremultiplied(std::span<const float, kChannels> channels) {
    std::array<std::uint32_t, kChannels> level{};
    for (std::size_t i = 0; i < kChannels; ++i) {
        level[i] = static_cast<std::uint32_t>(std::clamp(std::lround(channels[i]), 0L, 255L));
    }
    const std::uint32_t alpha = level[0];
    return (alpha << 24) | (std::min(level[1], alpha) << 16) | (std::min(level[2], alpha) << 8) |
           std::min(level[3], alpha);
}

// The source resampled to `size` by area averaging: horizontally into a float plane, then vertically, rounded once.
QImage areaAverage(const QImage& source, QSize size) {
    const ConstPixelSpan input(source);
    QImage result = makeCanonicalImage(size, 1.0);
    const PixelSpan output(result);
    if (input.isEmpty() || output.isEmpty()) {
        return {};
    }
    const std::size_t sourceHeight = toIndex(input.height());
    const std::size_t width = toIndex(output.width());
    const Scratch plane = allocateScratch(sourceHeight * width * kChannels);
    const Scratch columns = allocateScratch(width * kChannels);
    if (!plane || !columns) {
        return {};
    }

    for (std::size_t y = 0; y < sourceHeight; ++y) {
        const std::span<const std::uint32_t> row = input.row(static_cast<int>(y));
        for (std::size_t x = 0; x < width; ++x) {
            const std::span<float, kChannels> mean(plane.get() + (y * width + x) * kChannels, kChannels);
            std::ranges::fill(mean, 0.0F);
            forEachCoveredSource(static_cast<int>(x), input.width(), output.width(), [&](int sourceX, double weight) {
                const std::uint32_t pixel = row[toIndex(sourceX)];
                mean[0] += static_cast<float>(weight * (pixel >> 24));
                mean[1] += static_cast<float>(weight * ((pixel >> 16) & 0xffU));
                mean[2] += static_cast<float>(weight * ((pixel >> 8) & 0xffU));
                mean[3] += static_cast<float>(weight * (pixel & 0xffU));
            });
        }
    }
    const std::span<float> mean(columns.get(), width * kChannels);
    for (int y = 0; y < output.height(); ++y) {
        std::ranges::fill(mean, 0.0F);
        forEachCoveredSource(y, input.height(), output.height(), [&](int sourceY, double weight) {
            const float* sourceRow = plane.get() + toIndex(sourceY) * width * kChannels;
            for (std::size_t i = 0; i < mean.size(); ++i) {
                mean[i] += static_cast<float>(weight * sourceRow[i]);
            }
        });
        const std::span<std::uint32_t> row = output.row(y);
        for (std::size_t x = 0; x < width; ++x) {
            row[x] = packPremultiplied(mean.subspan(x * kChannels).first<kChannels>());
        }
    }
    return result;
}

// The source enlarged to `size` by nearest-neighbour sampling at pixel centres.
QImage nearestEnlarged(const QImage& source, QSize size, qreal devicePixelRatio) {
    const ConstPixelSpan input(source);
    QImage result = makeCanonicalImage(size, devicePixelRatio);
    const PixelSpan output(result);
    if (input.isEmpty() || output.isEmpty()) {
        return {};
    }
    const auto nearest = [](int destination, int sourceCount, int destinationCount) {
        const std::int64_t centre = 2 * static_cast<std::int64_t>(destination) + 1;
        return static_cast<int>(
            std::min<std::int64_t>(sourceCount - 1, centre * sourceCount / (2LL * destinationCount)));
    };
    for (int y = 0; y < output.height(); ++y) {
        const std::span<const std::uint32_t> sourceRow = input.row(nearest(y, input.height(), output.height()));
        const std::span<std::uint32_t> row = output.row(y);
        for (int x = 0; x < output.width(); ++x) {
            row[toIndex(x)] = sourceRow[toIndex(nearest(x, input.width(), output.width()))];
        }
    }
    return result;
}

} // namespace

PixelateSizes pixelateSizes(QSize regionPixelSize, QSizeF regionPointSize) {
    PixelateSizes sizes;
    sizes.first =
        QSize(std::max(1, regionPixelSize.width() / kPixelBlock), std::max(1, regionPixelSize.height() / kPixelBlock));
    sizes.second = QSize(std::max(1, sizes.first.width() / kSecondStepDivisor),
                         std::max(1, sizes.first.height() / kSecondStepDivisor));
    sizes.output = QSize(outputExtent(regionPointSize.width()), outputExtent(regionPointSize.height()));
    return sizes;
}

QImage pixelate(const QImage& region, QSizeF regionPointSize) {
    const bool sizeIsUsable = std::isfinite(regionPointSize.width()) && std::isfinite(regionPointSize.height()) &&
                              regionPointSize.width() > 0.0 && regionPointSize.height() > 0.0;
    if (!sizeIsUsable || ConstPixelSpan(region).isEmpty()) {
        return {};
    }
    const PixelateSizes sizes = pixelateSizes(region.size(), regionPointSize);
    const QImage first = areaAverage(region, sizes.first);
    const QImage second = areaAverage(first, sizes.second);
    return nearestEnlarged(second, sizes.output, kOutputDevicePixelRatio);
}

} // namespace ariadshot::render
