// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the shadow table and the pass orders follow macshot/Services/BeautifyRenderer.swift:493-591 and :840-895
// at b4d4f3a (see PROVENANCE.md).

#include "render/beautify/Shadow.h"

#include "render/CanonicalImage.h"
#include "render/effects/Blur.h"
#include "render/pixel/AlphaMask.h"
#include "render/pixel/PixelSpan.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace ariadshot::render {

namespace {

// t = clamp(radius / 100, 0, 1) (macshot/Services/BeautifyRenderer.swift:495@b4d4f3a).
constexpr double kRadiusForFullOpacity = 100.0;

// Ambient shadow: alpha 0.42 + 0.38 t (:496), offset min(4 + 0.35 radius, 18) (:507), blur radius (:543, :586).
constexpr double kAmbientAlphaBase = 0.42;
constexpr double kAmbientAlphaGain = 0.38;
constexpr double kAmbientOffsetBase = 4.0;
constexpr double kAmbientOffsetPerRadius = 0.35;
constexpr double kAmbientOffsetLimit = 18.0;

// Contact shadow: alpha 0.20 + 0.30 t (:502), offset min(2 + 0.12 radius, 10) (:512), blur min(4 + 0.18 radius, 16)
// (:517), all of macshot/Services/BeautifyRenderer.swift at b4d4f3a.
constexpr double kContactAlphaBase = 0.20;
constexpr double kContactAlphaGain = 0.30;
constexpr double kContactOffsetBase = 2.0;
constexpr double kContactOffsetPerRadius = 0.12;
constexpr double kContactOffsetLimit = 10.0;
constexpr double kContactBlurBase = 4.0;
constexpr double kContactBlurPerRadius = 0.18;
constexpr double kContactBlurLimit = 16.0;

// The largest offset or blur, in pixels of the canvas it is painted on, that is painted: the blur refuses a sigma above
// it, and it keeps every conversion to an int below within range.
constexpr double kLargestShadowExtent = 1.0e6;

bool isUsable(const ShadowParameters& shadow) {
    return std::isfinite(shadow.alpha) && std::isfinite(shadow.offset) && std::isfinite(shadow.blur) &&
           shadow.alpha >= 0.0 && shadow.alpha <= 1.0 && shadow.offset >= 0.0 && shadow.blur >= 0.0;
}

// How far, in pixels, a blur of the given sigma spreads: the sum of the box radii.
int blurReach(double sigma) {
    int reach = 0;
    for (const int width : boxBlurWidths(sigma)) {
        reach += (width - 1) / 2;
    }
    return reach;
}

// The shadow and then the caster, which is one pass of MacShot's transparency layers.
bool paintPass(QImage& canvas, const QImage& caster, QPoint origin, const ShadowParameters& shadow) {
    return paintShadow(canvas, caster, origin, shadow) && compositeOver(canvas, caster, origin);
}

} // namespace

ShadowParameters ambientShadow(double radius) {
    if (!(radius > 0.0)) {
        return {};
    }
    const double t = std::clamp(radius / kRadiusForFullOpacity, 0.0, 1.0);
    return {.alpha = kAmbientAlphaBase + t * kAmbientAlphaGain,
            .offset = std::min(kAmbientOffsetBase + radius * kAmbientOffsetPerRadius, kAmbientOffsetLimit),
            .blur = radius};
}

ShadowParameters contactShadow(double radius) {
    if (!(radius > 0.0)) {
        return {};
    }
    const double t = std::clamp(radius / kRadiusForFullOpacity, 0.0, 1.0);
    return {.alpha = kContactAlphaBase + t * kContactAlphaGain,
            .offset = std::min(kContactOffsetBase + radius * kContactOffsetPerRadius, kContactOffsetLimit),
            .blur = std::min(kContactBlurBase + radius * kContactBlurPerRadius, kContactBlurLimit)};
}

bool paintShadow(QImage& canvas, const QImage& caster, QPoint origin, const ShadowParameters& shadow) {
    if (!isUsable(shadow) || !canViewPixels(caster) || !canViewPixels(canvas)) {
        return false;
    }
    if (shadow.alpha == 0.0) {
        return true;
    }
    // Offset and blur are in points and the canvas works in pixels. The products are checked before anything is
    // converted to an int: a large device pixel ratio turns an ordinary value into a huge one, and a ratio that is not
    // a number or is infinite fails the same comparisons.
    const double scale = canvas.devicePixelRatio();
    const double sigma = shadow.blur * scale;
    const double offset = shadow.offset * scale;
    if (!(sigma <= kLargestShadowExtent) || !(offset <= kLargestShadowExtent)) {
        return false;
    }

    // The mask holds the caster with room for the blur on every side and for the move down.
    const int margin = blurReach(sigma);
    const QImage mask = makeShadowMask(caster, shadow.alpha, margin, static_cast<int>(std::ceil(offset)));
    if (mask.isNull()) {
        return false;
    }
    const QImage blurred = gaussianBlur(mask, sigma);
    return !blurred.isNull() && compositeShadowMask(canvas, blurred, origin, margin, offset);
}

bool paintRoundedImageWithShadow(QImage& canvas, const QImage& roundedImage, QPoint origin, double radius) {
    if (!canViewPixels(canvas) || !canViewPixels(roundedImage)) {
        return false;
    }
    if (radius > 0.0 && !(paintPass(canvas, roundedImage, origin, ambientShadow(radius)) &&
                          paintPass(canvas, roundedImage, origin, contactShadow(radius)))) {
        return false;
    }
    return compositeOver(canvas, roundedImage, origin);
}

bool paintWindowBodyShadow(QImage& canvas, const QRect& body, double cornerRadius, double radius) {
    if (!canViewPixels(canvas) || body.isEmpty()) {
        return false;
    }
    if (!(radius > 0.0)) {
        return true;
    }
    const double cornerPixels = std::max(0.0, cornerRadius) * canvas.devicePixelRatio();
    QImage bodyImage = makeCanonicalImage(body.size(), 1.0);
    if (bodyImage.isNull()) {
        return false;
    }
    QPainterPath shape;
    shape.addRoundedRect(QRectF(QPointF(), QSizeF(body.size())), cornerPixels, cornerPixels);
    QPainter painter(&bodyImage);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillPath(shape, Qt::white);
    painter.end();

    return paintPass(canvas, bodyImage, body.topLeft(), contactShadow(radius)) &&
           paintPass(canvas, bodyImage, body.topLeft(), ambientShadow(radius));
}

bool paintSnappedWindowShadow(QImage& canvas, const QImage& windowImage, QPoint origin, double radius) {
    if (!canViewPixels(canvas) || !canViewPixels(windowImage)) {
        return false;
    }
    if (radius > 0.0 && !(paintPass(canvas, windowImage, origin, contactShadow(radius)) &&
                          paintPass(canvas, windowImage, origin, ambientShadow(radius)))) {
        return false;
    }
    return compositeOver(canvas, windowImage, origin);
}

} // namespace ariadshot::render
