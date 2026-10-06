// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>

namespace ariadshot::render {

// The pixel sizes of the three steps of MacShot's pixelate bake (macshot/Model/Annotation.swift:1990-2011@b4d4f3a).
struct PixelateSizes {
    QSize first;  // the region reduced by a factor of 8, at least 1 x 1
    QSize second; // half of the first, at least 1 x 1
    QSize output; // twice the region's size in points, truncated, at least 1 x 1
};

// The step sizes for a region of the given pixel size whose rectangle measures regionPointSize points. Both sizes
// must be positive. Thread: any.
[[nodiscard]] PixelateSizes pixelateSizes(QSize regionPixelSize, QSizeF regionPointSize);

// The pixelate effect of a region (spec 03 §4.2): the region is reduced by area averaging to the first size, again to
// the second, and enlarged by nearest-neighbour sampling to the output size, which gives crisp blocks. Each step
// rounds to 8 bits as MacShot's bitmap contexts do. The result is a canonical image of the output size and a device
// pixel ratio of 2, so it covers regionPointSize when drawn at its device-independent size.
//
// Returns a null image when region is null or not QImage::Format_ARGB32_Premultiplied, when a point size is not
// positive and finite, or when memory runs out. Thread: any.
[[nodiscard]] QImage pixelate(const QImage& region, QSizeF regionPointSize);

} // namespace ariadshot::render
