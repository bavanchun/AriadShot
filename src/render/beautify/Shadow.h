// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>

namespace ariadshot::render {

// One black shadow of the calibrated pair. offset is the downward shift and blur the amount of blur, both in points
// of the canvas it is painted on; alpha is the shadow's opacity in [0, 1]. Like spec 03 §4.1, AriadShot treats a blur
// value as the Gaussian sigma [HYPOTHESIS].
struct ShadowParameters {
    double alpha = 0.0;
    double offset = 0.0;
    double blur = 0.0;
};

// The two shadows MacShot casts for a shadow radius in points
// (macshot/Services/BeautifyRenderer.swift:493-518@b4d4f3a), with t = clamp(radius / 100, 0, 1): the ambient shadow has
// alpha 0.42 + 0.38 t, offset min(4 + 0.35 radius, 18) and blur radius; the contact shadow has alpha 0.20 + 0.30 t,
// offset min(2 + 0.12 radius, 10) and blur min(4 + 0.18 radius, 16). Both are off (all zero) when radius is not
// positive. Thread: any.
[[nodiscard]] ShadowParameters ambientShadow(double radius);
[[nodiscard]] ShadowParameters contactShadow(double radius);

// The painters below composite into a canonical canvas (QImage::Format_ARGB32_Premultiplied) with premultiplied
// source-over. Positions and sizes are in canvas pixels; the shadow parameters, the shadow radius and the corner radius
// are in points and scale with the canvas's device pixel ratio. Nothing is painted outside the canvas, and a shadow
// that runs off the canvas is the part of the full shadow, as if the canvas were larger. Each returns false, leaving
// the canvas unchanged unless memory runs out midway, when an image is null or not canonical, or a value is unusable.
// Thread: any.

// Paints the shadow cast by the alpha of `caster` placed at `origin`: a black layer of that alpha times
// shadow.alpha, blurred, and moved down by shadow.offset (a fraction of a pixel mixes two rows). The caster itself
// is not painted. Usable values are finite, alpha in [0, 1], offset and blur from 0 to one million points.
[[nodiscard]] bool paintShadow(QImage& canvas, const QImage& caster, QPoint origin, const ShadowParameters& shadow);

// MacShot's rounded mode (macshot/Services/BeautifyRenderer.swift:538-563@b4d4f3a): the ambient shadow and the image,
// the contact shadow and the image, then the image once more, crisp. Both shadows are cast from the alpha of
// roundedImage, which the caller has already clipped to its rounded rectangle, so no caster rim appears. A radius that
// is not positive paints only the crisp image.
[[nodiscard]] bool paintRoundedImageWithShadow(QImage& canvas, const QImage& roundedImage, QPoint origin,
                                               double radius);

// MacShot's window mode (macshot/Services/BeautifyRenderer.swift:566-591@b4d4f3a): the contact shadow and then the
// ambient shadow of an opaque white window body of the given pixel rectangle and corner radius, the body painted with
// each, under the window the caller draws next. A radius that is not positive paints nothing.
[[nodiscard]] bool paintWindowBodyShadow(QImage& canvas, const QRect& body, double cornerRadius, double radius);

// MacShot's snapped window (macshot/Services/BeautifyRenderer.swift:867-884@b4d4f3a): the contact shadow and the
// window image, the ambient shadow and the image, then the image once more, crisp, all cast from the window image's own
// alpha. A radius that is not positive paints only the crisp image.
[[nodiscard]] bool paintSnappedWindowShadow(QImage& canvas, const QImage& windowImage, QPoint origin, double radius);

} // namespace ariadshot::render
