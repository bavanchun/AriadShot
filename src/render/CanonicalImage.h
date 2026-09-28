// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QSize>

namespace ariadshot::render {

// The single factory for canonical images, the one pixel source of the still-image rendering contract
// (docs/spec/03-rendering-contracts.md): format ARGB32_Premultiplied, QColorSpace::SRgb attached, device pixel ratio
// equal to the capture scale, and a fully transparent fill.
//
// Returns a null image when pixelSize is empty, devicePixelRatio is not a positive finite number, or the pixel
// buffer cannot be allocated. Thread: any.
[[nodiscard]] QImage makeCanonicalImage(QSize pixelSize, qreal devicePixelRatio);

} // namespace ariadshot::render
