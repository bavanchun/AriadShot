// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QSize>

#include <array>

namespace ariadshot::render {

// The Gaussian sigma, in pixels, of MacShot's censor blur for a region of the given pixel size: the larger of 10 and
// 3 % of the region's shorter side (macshot/Model/Annotation.swift:2256@b4d4f3a). MacShot passes it to Core Image as
// inputRadius; like spec 03 §4.1, AriadShot treats that value as sigma [HYPOTHESIS]. Thread: any.
[[nodiscard]] double censorBlurSigma(QSize regionPixelSize);

// The widths, in pixels and all odd, of the three successive box passes per axis that approximate a Gaussian of the
// given sigma: the standard construction of cascaded box filters (Wells 1986) with wIdeal = sqrt(12 sigma^2 / 3 + 1),
// wl the odd integer at or below it, wu = wl + 2, and m = round((12 sigma^2 - 3 wl^2 - 12 wl - 9) / (-4 wl - 4)) boxes
// of width wl, the rest of width wu. A sigma that is not positive and finite gives three boxes of width 1.
// Thread: any.
[[nodiscard]] std::array<int, 3> boxBlurWidths(double sigma);

// A separable Gaussian blur of a canonical image: three box passes per axis (boxBlurWidths) on the premultiplied
// channels, clamp-to-edge sampling so the edges do not darken (as MacShot's CIAffineClamp before CIGaussianBlur,
// macshot/Model/Annotation.swift:2253-2272@b4d4f3a), one rounding at the end. The result is a new canonical image of
// the same size and device pixel ratio; the working memory is 16 bytes per pixel.
//
// Returns a null image when source is null or not QImage::Format_ARGB32_Premultiplied, when sigma is negative, not a
// number or above one million, or when memory runs out. A sigma of 0 returns the pixels unchanged. Thread: any.
[[nodiscard]] QImage gaussianBlur(const QImage& source, double sigma);

} // namespace ariadshot::render
