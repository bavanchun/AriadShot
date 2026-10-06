// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>

#include <array>

namespace ariadshot::render {

// Three box passes per axis over the premultiplied channels of a canonical image: the passes of radii[0], radii[1] and
// radii[2] in turn over every row, then over every column. A pass of radius r replaces each pixel by the mean of the
// 2 r + 1 pixels centred on it, with the first and last pixel of the line repeated past its ends (clamp-to-edge), so
// the edges do not darken. The sums are kept in double and updated by one pixel entering and one leaving, so a constant
// line stays exactly constant, and the channels are rounded to 8 bits once, at the end. Every radius from 0 up is
// valid.
//
// The result is a new canonical image of the same size and device pixel ratio; the working memory is 16 bytes per
// pixel. Returns a null image when source is null or not QImage::Format_ARGB32_Premultiplied, when a radius is
// negative, or when memory runs out. Thread: any.
[[nodiscard]] QImage boxBlur(const QImage& source, const std::array<int, 3>& radii);

} // namespace ariadshot::render
