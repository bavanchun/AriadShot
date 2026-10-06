// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the censor blur's sigma rule follows macshot/Model/Annotation.swift:2253-2272@b4d4f3a (see
// PROVENANCE.md).

#include "render/effects/Blur.h"

#include "render/pixel/BoxBlur.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ariadshot::render {

namespace {

// The censor blur's sigma is at least this many pixels (macshot/Model/Annotation.swift:2256@b4d4f3a) ...
constexpr double kCensorBlurMinimumSigma = 10.0;
// ... and at least this fraction of the region's shorter side (macshot/Model/Annotation.swift:2256@b4d4f3a).
constexpr double kCensorBlurSigmaPerSide = 0.03;

// Larger sigmas are refused: the box radii would exceed any image by orders of magnitude.
constexpr double kLargestSigma = 1.0e6;

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
    const std::array<int, 3> widths = boxBlurWidths(sigma);
    return boxBlur(source, {(widths[0] - 1) / 2, (widths[1] - 1) / 2, (widths[2] - 1) / 2});
}

} // namespace ariadshot::render
