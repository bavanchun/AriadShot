// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QSize>

namespace ariadshot::render {

// A canonical image of `size` pixels and a device pixel ratio of 1 made from `source` by area averaging: every
// destination pixel is the mean of the source area it covers, each source pixel weighted by the fraction of the
// destination pixel it covers, channel by channel. The mean is taken horizontally into a float plane, then vertically,
// and rounded to 8 bits once, halves up. A size equal to the source's gives the same pixels back.
//
// Returns a null image when source is null or not QImage::Format_ARGB32_Premultiplied, when size is empty, or when
// memory runs out. Thread: any.
[[nodiscard]] QImage areaAverage(const QImage& source, QSize size);

// A canonical image of `size` pixels and the given device pixel ratio made from `source` by nearest-neighbour
// sampling: destination pixel d of n along an axis of c source pixels takes the source pixel that contains the
// destination pixel's centre, floor((2 d + 1) c / (2 n)), so an enlargement gives crisp blocks.
//
// Returns a null image when source is null or not QImage::Format_ARGB32_Premultiplied, when size is empty, when
// devicePixelRatio is not a positive finite number, or when memory runs out. Thread: any.
[[nodiscard]] QImage enlargeNearest(const QImage& source, QSize size, qreal devicePixelRatio);

} // namespace ariadshot::render
