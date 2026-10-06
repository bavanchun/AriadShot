// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the step sizes follow macshot/Model/Annotation.swift:1953-2020@b4d4f3a (see PROVENANCE.md).

#include "render/effects/Pixelate.h"

#include "render/pixel/PixelSpan.h"
#include "render/pixel/Resample.h"

#include <algorithm>
#include <cmath>

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

int outputExtent(double points) {
    return std::max(1, static_cast<int>(std::min(points * kOutputPixelsPerPoint, kLargestExtent)));
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
    if (!sizeIsUsable || !canViewPixels(region)) {
        return {};
    }
    const PixelateSizes sizes = pixelateSizes(region.size(), regionPointSize);
    const QImage first = areaAverage(region, sizes.first);
    const QImage second = areaAverage(first, sizes.second);
    return enlargeNearest(second, sizes.output, kOutputDevicePixelRatio);
}

} // namespace ariadshot::render
