// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/pixel/Resample.h"

#include "render/CanonicalImage.h"
#include "render/pixel/FloatChannels.h"
#include "render/pixel/PixelSpan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ariadshot::render {

namespace {

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

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

} // namespace

QImage areaAverage(const QImage& source, QSize size) {
    const ConstPixelSpan input(source);
    QImage result = makeCanonicalImage(size, 1.0);
    const PixelSpan output(result);
    if (input.isEmpty() || output.isEmpty()) {
        return {};
    }
    const std::size_t sourceHeight = toIndex(input.height());
    const std::size_t width = toIndex(output.width());
    const FloatBuffer plane = allocateFloats(sourceHeight * width * kPixelChannels);
    const FloatBuffer columns = allocateFloats(width * kPixelChannels);
    if (!plane || !columns) {
        return {};
    }

    for (std::size_t y = 0; y < sourceHeight; ++y) {
        const std::span<const std::uint32_t> row = input.row(static_cast<std::int64_t>(y));
        for (std::size_t x = 0; x < width; ++x) {
            const std::span<float, kPixelChannels> mean(plane.get() + (y * width + x) * kPixelChannels, kPixelChannels);
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
    const std::span<float> mean(columns.get(), width * kPixelChannels);
    for (int y = 0; y < output.height(); ++y) {
        std::ranges::fill(mean, 0.0F);
        forEachCoveredSource(y, input.height(), output.height(), [&](int sourceY, double weight) {
            const float* sourceRow = plane.get() + toIndex(sourceY) * width * kPixelChannels;
            for (std::size_t i = 0; i < mean.size(); ++i) {
                mean[i] += static_cast<float>(weight * sourceRow[i]);
            }
        });
        const std::span<std::uint32_t> row = output.row(y);
        for (std::size_t x = 0; x < width; ++x) {
            row[x] = packPremultiplied(mean.subspan(x * kPixelChannels).first<kPixelChannels>());
        }
    }
    return result;
}

QImage enlargeNearest(const QImage& source, QSize size, qreal devicePixelRatio) {
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

} // namespace ariadshot::render
