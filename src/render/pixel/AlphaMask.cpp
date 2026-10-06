// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/pixel/AlphaMask.h"

#include "render/CanonicalImage.h"
#include "render/pixel/PixelSpan.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ariadshot::render {

namespace {

// What a QSize extent and a row index hold.
constexpr std::int64_t kLargestExtent = std::numeric_limits<int>::max();
constexpr double kLargestRowOffset = static_cast<double>(std::numeric_limits<int>::max());

// Premultiplied source-over: each channel is source + destination * (255 - source alpha) / 255, rounded to nearest.
std::uint32_t over(std::uint32_t source, std::uint32_t destination) {
    const std::uint32_t inverse = 255 - (source >> 24);
    std::uint32_t result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const std::uint32_t sourceChannel = (source >> shift) & 0xffU;
        const std::uint32_t destinationChannel = (destination >> shift) & 0xffU;
        result |= (sourceChannel + (destinationChannel * inverse + 127) / 255) << shift;
    }
    return result;
}

// The part of a run of `length` pixels that starts at `position` on a line of `limit` pixels and lies on that line, as
// the indices [first, last) into the run. Both ends are within [0, length]; the run is empty when none of it is on the
// line. The positions are 64-bit, so the sums of two ints cannot overflow.
struct Visible {
    std::int64_t first;
    std::int64_t last;

    [[nodiscard]] bool isEmpty() const { return last <= first; }
    [[nodiscard]] std::size_t count() const { return static_cast<std::size_t>(last - first); }
};

Visible visibleRun(std::int64_t position, std::int64_t length, std::int64_t limit) {
    return {.first = std::max<std::int64_t>(0, -position), .last = std::min(length, limit - position)};
}

} // namespace

bool compositeOver(QImage& canvas, const QImage& image, QPoint origin) {
    const PixelSpan target(canvas);
    const ConstPixelSpan source(image);
    if (target.isEmpty() || source.isEmpty()) {
        return false;
    }
    const Visible columns = visibleRun(origin.x(), source.width(), target.width());
    const Visible rows = visibleRun(origin.y(), source.height(), target.height());
    if (columns.isEmpty() || rows.isEmpty()) {
        return true;
    }
    for (std::int64_t y = rows.first; y < rows.last; ++y) {
        const std::span<const std::uint32_t> from =
            source.row(y).subspan(static_cast<std::size_t>(columns.first), columns.count());
        const std::span<std::uint32_t> to =
            target.row(origin.y() + y).subspan(static_cast<std::size_t>(origin.x() + columns.first), columns.count());
        for (std::size_t i = 0; i < to.size(); ++i) {
            to[i] = over(from[i], to[i]);
        }
    }
    return true;
}

QImage makeShadowMask(const QImage& caster, double opacity, int margin, int extraRows) {
    const ConstPixelSpan source(caster);
    if (source.isEmpty() || !(opacity >= 0.0 && opacity <= 1.0) || margin < 0 || extraRows < 0) {
        return {};
    }
    const std::int64_t width = std::int64_t{source.width()} + 2 * std::int64_t{margin};
    const std::int64_t height = std::int64_t{source.height()} + 2 * std::int64_t{margin} + extraRows;
    if (width > kLargestExtent || height > kLargestExtent) {
        return {};
    }
    QImage mask = makeCanonicalImage(QSize(static_cast<int>(width), static_cast<int>(height)), 1.0);
    const PixelSpan target(mask);
    if (target.isEmpty()) {
        return {};
    }
    for (int y = 0; y < source.height(); ++y) {
        const std::span<const std::uint32_t> from = source.row(y);
        const std::span<std::uint32_t> to =
            target.row(std::int64_t{margin} + y).subspan(static_cast<std::size_t>(margin), from.size());
        for (std::size_t x = 0; x < from.size(); ++x) {
            to[x] = static_cast<std::uint32_t>(std::lround((from[x] >> 24) * opacity)) << 24;
        }
    }
    return mask;
}

bool compositeShadowMask(QImage& canvas, const QImage& mask, QPoint casterOrigin, int margin, double rowOffset) {
    const PixelSpan target(canvas);
    const ConstPixelSpan layer(mask);
    if (target.isEmpty() || layer.isEmpty() || margin < 0 || !(rowOffset >= 0.0 && rowOffset <= kLargestRowOffset)) {
        return false;
    }
    const auto wholeRows = static_cast<std::int64_t>(std::floor(rowOffset));
    const double partialRow = rowOffset - static_cast<double>(wholeRows);
    const std::int64_t layerX = std::int64_t{casterOrigin.x()} - margin;
    const std::int64_t layerY = std::int64_t{casterOrigin.y()} - margin;
    const Visible columns = visibleRun(layerX, layer.width(), target.width());
    const Visible rows = visibleRun(layerY, layer.height(), target.height());
    if (columns.isEmpty() || rows.isEmpty()) {
        return true;
    }

    // Each canvas row takes the mask moved down: a mix of the two mask rows it lands between.
    for (std::int64_t y = rows.first; y < rows.last; ++y) {
        const std::span<const std::uint32_t> upper = layer.row(y - wholeRows);
        const std::span<const std::uint32_t> lower = layer.row(y - wholeRows - 1);
        const std::span<std::uint32_t> to =
            target.row(layerY + y).subspan(static_cast<std::size_t>(layerX + columns.first), columns.count());
        for (std::size_t i = 0; i < to.size(); ++i) {
            const auto x = static_cast<std::size_t>(columns.first) + i;
            const double upperAlpha = upper.empty() ? 0.0 : (upper[x] >> 24);
            const double lowerAlpha = lower.empty() ? 0.0 : (lower[x] >> 24);
            const double alpha = (1.0 - partialRow) * upperAlpha + partialRow * lowerAlpha;
            to[i] = over(static_cast<std::uint32_t>(std::lround(alpha)) << 24, to[i]);
        }
    }
    return true;
}

} // namespace ariadshot::render
