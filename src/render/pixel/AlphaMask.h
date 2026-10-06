// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QPoint>

namespace ariadshot::render {

// Premultiplied source-over of `image` onto `canvas`, the image's top-left pixel at `origin` on the canvas: each
// channel becomes source + destination * (255 - source alpha) / 255, rounded to nearest. Only the part of the image
// that lies on the canvas is drawn; the origin may be any point, however far outside, and positions are worked out in
// 64 bits.
//
// Returns false, leaving the canvas unchanged, when either image is null or not QImage::Format_ARGB32_Premultiplied.
// Thread: any (one thread per image).
[[nodiscard]] bool compositeOver(QImage& canvas, const QImage& image, QPoint origin);

// The mask of the shadow of `caster`: a canonical image of device pixel ratio 1, (caster width + 2 margin) pixels wide
// and (caster height + 2 margin + extraRows) high, holding the caster's alpha times opacity (rounded to nearest) on
// black pixels with the caster's top-left pixel at (margin, margin): room to blur it on every side and to move it down
// by extraRows rows. Every other pixel is transparent.
//
// Returns a null image when caster is null or not QImage::Format_ARGB32_Premultiplied, when opacity is not in [0, 1],
// when margin or extraRows is negative, when either extent would exceed the largest int (checked in 64 bits, before any
// memory is requested), or when memory runs out. Thread: any.
[[nodiscard]] QImage makeShadowMask(const QImage& caster, double opacity, int margin, int extraRows);

// Composites a shadow mask onto `canvas` as black pixels of the mask's alpha, with premultiplied source-over. `mask`
// has the layout of makeShadowMask, usually after blurring: the caster's top-left pixel at (margin, margin).
// casterOrigin is where that pixel is on the canvas. The mask is moved down by rowOffset rows; a fractional part f
// mixes the two mask rows each canvas row falls between, alpha = (1 - f) * the row above + f * the row above that,
// rounded to nearest. Only the part of the mask that lies on the canvas is drawn; casterOrigin may be any point and
// margin and the move any size, and positions are worked out in 64 bits.
//
// Returns false, leaving the canvas unchanged, when an image is null or not QImage::Format_ARGB32_Premultiplied, when
// margin is negative, or when rowOffset is not a number, is negative or is above the largest int. Thread: any (one
// thread per image).
[[nodiscard]] bool compositeShadowMask(QImage& canvas, const QImage& mask, QPoint casterOrigin, int margin,
                                       double rowOffset);

} // namespace ariadshot::render
