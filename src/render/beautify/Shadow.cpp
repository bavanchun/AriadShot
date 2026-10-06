// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the shadow table and the pass orders follow macshot/Services/BeautifyRenderer.swift:493-591 and :840-895
// at b4d4f3a (see PROVENANCE.md).

#include "render/beautify/Shadow.h"

#include "render/CanonicalImage.h"
#include "render/effects/Blur.h"
#include "render/pixel/PixelSpan.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

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

// The largest offset or blur, in points, that is painted; it keeps the pixel arithmetic below within an int.
constexpr double kLargestShadowExtent = 1.0e6;

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

// Premultiplied source-over: each channel is source + destination * (255 - source alpha) / 255, rounded to nearest.
std::uint32_t over(std::uint32_t source, std::uint32_t destination) {
    const std::uint32_t inverse = 255 - (source >> 24);
    std::uint32_t result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const std::uint32_t sourceChannel = (source >> shift) & 0xffU;
        const std::uint32_t destinationChannel = (destination >> shift) & 0xffU;
        result |= (sourceChannel + (destinationChannel * inverse + 127) / 255) << shift;
    }
    return result;
}

bool isUsable(const ShadowParameters& shadow) {
    return std::isfinite(shadow.alpha) && std::isfinite(shadow.offset) && std::isfinite(shadow.blur) &&
           shadow.alpha >= 0.0 && shadow.alpha <= 1.0 && shadow.offset >= 0.0 &&
           shadow.offset <= kLargestShadowExtent && shadow.blur >= 0.0 && shadow.blur <= kLargestShadowExtent;
}

// How far, in pixels, a blur of the given sigma spreads: the sum of the box radii.
int blurReach(double sigma) {
    int reach = 0;
    for (const int width : boxBlurWidths(sigma)) {
        reach += (width - 1) / 2;
    }
    return reach;
}

// The columns [first, last) of a layer of `layerWidth` pixels placed at `layerLeft` that fall on a canvas of
// `canvasWidth` pixels.
struct Columns {
    int first;
    int last;
};

Columns visibleColumns(int layerLeft, int layerWidth, int canvasWidth) {
    return {.first = std::max(0, -layerLeft), .last = std::min(layerWidth, canvasWidth - layerLeft)};
}

// Paints `image` over the canvas with its top-left at `origin`, clipped to the canvas.
bool drawImage(QImage& canvas, const QImage& image, QPoint origin) {
    const PixelSpan target(canvas);
    const ConstPixelSpan source(image);
    if (target.isEmpty() || source.isEmpty()) {
        return false;
    }
    const auto [first, last] = visibleColumns(origin.x(), source.width(), target.width());
    for (int y = std::max(0, -origin.y()); y < source.height() && origin.y() + y < target.height(); ++y) {
        const std::span<const std::uint32_t> sourceRow = source.row(y);
        const std::span<std::uint32_t> targetRow = target.row(origin.y() + y);
        for (int x = first; x < last; ++x) {
            std::uint32_t& pixel = targetRow[toIndex(origin.x() + x)];
            pixel = over(sourceRow[toIndex(x)], pixel);
        }
    }
    return true;
}

// The shadow and then the caster, which is one pass of MacShot's transparency layers.
bool paintPass(QImage& canvas, const QImage& caster, QPoint origin, const ShadowParameters& shadow) {
    return paintShadow(canvas, caster, origin, shadow) && drawImage(canvas, caster, origin);
}

bool isCanonical(const QImage& image) { return !ConstPixelSpan(image).isEmpty(); }

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
    const ConstPixelSpan casterPixels(caster);
    if (!isUsable(shadow) || casterPixels.isEmpty() || !isCanonical(canvas)) {
        return false;
    }
    if (shadow.alpha == 0.0) {
        return true;
    }
    const double scale = canvas.devicePixelRatio();
    const double sigma = shadow.blur * scale;
    const double offset = shadow.offset * scale;
    const int wholeRows = static_cast<int>(std::floor(offset));
    const double partialRow = offset - wholeRows;

    // The layer holds the caster with room for the blur on every side and for the move down.
    const int margin = blurReach(sigma);
    const QSize layerSize(casterPixels.width() + 2 * margin,
                          casterPixels.height() + 2 * margin + static_cast<int>(std::ceil(offset)));
    QImage layer = makeCanonicalImage(layerSize, 1.0);
    const PixelSpan layerPixels(layer);
    if (layerPixels.isEmpty()) {
        return false;
    }
    for (int y = 0; y < casterPixels.height(); ++y) {
        const std::span<std::uint32_t> layerRow = layerPixels.row(margin + y);
        const std::span<const std::uint32_t> casterRow = casterPixels.row(y);
        for (int x = 0; x < casterPixels.width(); ++x) {
            const double alpha = (casterRow[toIndex(x)] >> 24) * shadow.alpha;
            layerRow[toIndex(margin + x)] = static_cast<std::uint32_t>(std::lround(alpha)) << 24;
        }
    }
    const QImage blurred = gaussianBlur(layer, sigma);
    const ConstPixelSpan blurredPixels(blurred);
    const PixelSpan canvasPixels(canvas);
    if (blurredPixels.isEmpty()) {
        return false;
    }

    // Each canvas row takes the blurred layer moved down: a mix of the two rows it lands between.
    const QPoint layerOrigin = origin - QPoint(margin, margin);
    const auto [first, last] = visibleColumns(layerOrigin.x(), layerSize.width(), canvasPixels.width());
    for (int y = std::max(0, -layerOrigin.y()); y < layerSize.height() && layerOrigin.y() + y < canvasPixels.height();
         ++y) {
        const std::span<const std::uint32_t> upper = blurredPixels.row(y - wholeRows);
        const std::span<const std::uint32_t> lower = blurredPixels.row(y - wholeRows - 1);
        const std::span<std::uint32_t> canvasRow = canvasPixels.row(layerOrigin.y() + y);
        for (int x = first; x < last; ++x) {
            const double upperAlpha = upper.empty() ? 0.0 : (upper[toIndex(x)] >> 24);
            const double lowerAlpha = lower.empty() ? 0.0 : (lower[toIndex(x)] >> 24);
            const double alpha = (1.0 - partialRow) * upperAlpha + partialRow * lowerAlpha;
            std::uint32_t& pixel = canvasRow[toIndex(layerOrigin.x() + x)];
            pixel = over(static_cast<std::uint32_t>(std::lround(alpha)) << 24, pixel);
        }
    }
    return true;
}

bool paintRoundedImageWithShadow(QImage& canvas, const QImage& roundedImage, QPoint origin, double radius) {
    if (!isCanonical(canvas) || !isCanonical(roundedImage)) {
        return false;
    }
    if (radius > 0.0 && !(paintPass(canvas, roundedImage, origin, ambientShadow(radius)) &&
                          paintPass(canvas, roundedImage, origin, contactShadow(radius)))) {
        return false;
    }
    return drawImage(canvas, roundedImage, origin);
}

bool paintWindowBodyShadow(QImage& canvas, const QRect& body, double cornerRadius, double radius) {
    if (!isCanonical(canvas) || body.isEmpty()) {
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
    if (!isCanonical(canvas) || !isCanonical(windowImage)) {
        return false;
    }
    if (radius > 0.0 && !(paintPass(canvas, windowImage, origin, contactShadow(radius)) &&
                          paintPass(canvas, windowImage, origin, ambientShadow(radius)))) {
        return false;
    }
    return drawImage(canvas, windowImage, origin);
}

} // namespace ariadshot::render
