// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"

#include <QColorSpace>

#include <cmath>

namespace ariadshot::render {

QImage makeCanonicalImage(QSize pixelSize, qreal devicePixelRatio) {
    if (pixelSize.isEmpty() || !std::isfinite(devicePixelRatio) || devicePixelRatio <= 0.0) {
        return {};
    }
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    return image;
}

} // namespace ariadshot::render
