// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the expected shadow values follow macshot/Services/BeautifyRenderer.swift:493-518@b4d4f3a, and the pass
// orders follow macshot/Services/BeautifyRenderer.swift:538-563, :566-591 and :867-884@b4d4f3a (see PROVENANCE.md).

#include "render/CanonicalImage.h"
#include "render/beautify/Shadow.h"
#include "render/pixel/PixelSpan.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

using ariadshot::render::ambientShadow;
using ariadshot::render::ConstPixelSpan;
using ariadshot::render::contactShadow;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::paintRoundedImageWithShadow;
using ariadshot::render::paintShadow;
using ariadshot::render::paintSnappedWindowShadow;
using ariadshot::render::paintWindowBodyShadow;
using ariadshot::render::PixelSpan;
using ariadshot::render::ShadowParameters;

namespace {

constexpr quint32 kOpaqueWhite = 0xffffffffU;
constexpr quint32 kOpaqueBlack = 0xff000000U;
// Opaque white at alpha 128, premultiplied.
constexpr quint32 kTranslucentWhite = 0x80808080U;

constexpr ShadowParameters shadowOf(double alpha, double offset, double blur) {
    return {.alpha = alpha, .offset = offset, .blur = blur};
}

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

QImage filledImage(QSize size, quint32 pixel, qreal devicePixelRatio = 1.0) {
    QImage image = makeCanonicalImage(size, devicePixelRatio);
    const PixelSpan view(image);
    for (int y = 0; y < view.height(); ++y) {
        std::ranges::fill(view.row(y), pixel);
    }
    return image;
}

quint32 pixelAt(const QImage& image, int x, int y) { return ConstPixelSpan(image).row(y)[toIndex(x)]; }

int alphaAt(const QImage& image, int x, int y) { return static_cast<int>(pixelAt(image, x, y) >> 24); }

// Premultiplied source-over: each channel is source + destination * (255 - source alpha) / 255, rounded to nearest.
quint32 over(quint32 source, quint32 destination) {
    const quint32 inverse = 255 - (source >> 24);
    quint32 result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const quint32 s = (source >> shift) & 0xffU;
        const quint32 d = (destination >> shift) & 0xffU;
        result |= (s + (d * inverse + 127) / 255) << shift;
    }
    return result;
}

void drawOver(QImage& canvas, const QImage& image, QPoint origin) {
    const PixelSpan target(canvas);
    const ConstPixelSpan source(image);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            quint32& pixel = target.row(origin.y() + y)[toIndex(origin.x() + x)];
            pixel = over(source.row(y)[toIndex(x)], pixel);
        }
    }
}

struct Mass {
    double alpha = 0.0;
    double centreX = 0.0;
    double centreY = 0.0;
};

// The total alpha and the alpha-weighted centre, in pixel-centre coordinates.
Mass massOf(const QImage& image) {
    Mass mass;
    double x = 0.0;
    double y = 0.0;
    for (int row = 0; row < image.height(); ++row) {
        for (int column = 0; column < image.width(); ++column) {
            const int alpha = alphaAt(image, column, row);
            mass.alpha += alpha;
            x += alpha * (column + 0.5);
            y += alpha * (row + 0.5);
        }
    }
    mass.centreX = x / mass.alpha;
    mass.centreY = y / mass.alpha;
    return mass;
}

bool isBlack(quint32 pixel) { return (pixel & 0x00ffffffU) == 0; }

// The sequence MacShot paints for a shadowed caster: for each shadow, the shadow and then the caster; the first of
// `crispImages` more draws of the caster follows.
void paintSequence(QImage& canvas, const QImage& caster, QPoint origin, const ShadowParameters& first,
                   const ShadowParameters& second, int crispImages) {
    QVERIFY(paintShadow(canvas, caster, origin, first));
    drawOver(canvas, caster, origin);
    QVERIFY(paintShadow(canvas, caster, origin, second));
    drawOver(canvas, caster, origin);
    for (int i = 0; i < crispImages; ++i) {
        drawOver(canvas, caster, origin);
    }
}

} // namespace

class ShadowTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void shadowTableFollowsTheCalibratedValues_data();
    void shadowTableFollowsTheCalibratedValues();
    void aShadowIsBlackAndKeepsTheCastersAlphaScaledByItsOpacity();
    void aShadowIsOffsetDownwardAndBlurredWithoutLosingMass();
    void aFractionalOffsetMixesTwoRows();
    void offsetAndBlurAreInPointsOfTheCanvasScale();
    void aShadowRunningOffTheCanvasMatchesTheSameShadowOnALargerCanvas();
    void refusesUnusableInput_data();
    void refusesUnusableInput();
    void roundedModePaintsAmbientThenContactThenTheCrispImage();
    void snappedWindowPaintsContactThenAmbientThenTheCrispImage();
    void windowBodyPaintsContactThenAmbientUnderAnOpaqueBody();
    void windowBodyCornersFollowTheRadiusInPoints();
    void aShadowRadiusOfZeroPaintsNoShadow();
};

void ShadowTest::shadowTableFollowsTheCalibratedValues_data() {
    QTest::addColumn<double>("radius");
    QTest::addColumn<ShadowParameters>("ambient");
    QTest::addColumn<ShadowParameters>("contact");

    // With t = clamp(r / 100, 0, 1): ambient alpha 0.42 + 0.38 t, offset min(4 + 0.35 r, 18), blur r; contact alpha
    // 0.20 + 0.30 t, offset min(2 + 0.12 r, 10), blur min(4 + 0.18 r, 16); both off for r <= 0:
    // macshot/Services/BeautifyRenderer.swift:493-518@b4d4f3a
    QTest::newRow("zero") << 0.0 << shadowOf(0, 0, 0) << shadowOf(0, 0, 0);
    QTest::newRow("negative") << -5.0 << shadowOf(0, 0, 0) << shadowOf(0, 0, 0);
    QTest::newRow("1") << 1.0 << shadowOf(0.4238, 4.35, 1) << shadowOf(0.203, 2.12, 4.18);
    QTest::newRow("10") << 10.0 << shadowOf(0.458, 7.5, 10) << shadowOf(0.23, 3.2, 5.8);
    QTest::newRow("30") << 30.0 << shadowOf(0.534, 14.5, 30) << shadowOf(0.29, 5.6, 9.4);
    QTest::newRow("40, ambient offset reaches its limit")
        << 40.0 << shadowOf(0.572, 18, 40) << shadowOf(0.32, 6.8, 11.2);
    QTest::newRow("50") << 50.0 << shadowOf(0.61, 18, 50) << shadowOf(0.35, 8, 13);
    QTest::newRow("100") << 100.0 << shadowOf(0.8, 18, 100) << shadowOf(0.5, 10, 16);
    QTest::newRow("150, t stays at 1") << 150.0 << shadowOf(0.8, 18, 150) << shadowOf(0.5, 10, 16);
}

void ShadowTest::shadowTableFollowsTheCalibratedValues() {
    QFETCH(double, radius);
    QFETCH(ShadowParameters, ambient);
    QFETCH(ShadowParameters, contact);

    const ShadowParameters actualAmbient = ambientShadow(radius);
    QCOMPARE(actualAmbient.alpha, ambient.alpha);
    QCOMPARE(actualAmbient.offset, ambient.offset);
    QCOMPARE(actualAmbient.blur, ambient.blur);
    const ShadowParameters actualContact = contactShadow(radius);
    QCOMPARE(actualContact.alpha, contact.alpha);
    QCOMPARE(actualContact.offset, contact.offset);
    QCOMPARE(actualContact.blur, contact.blur);
}

void ShadowTest::aShadowIsBlackAndKeepsTheCastersAlphaScaledByItsOpacity() {
    // The caster has alpha 255, 128 and 64 in three columns; with no offset and no blur the shadow is its alpha * 0.5.
    QImage caster = makeCanonicalImage(QSize(3, 1), 1.0);
    PixelSpan(caster).row(0)[0] = 0xffffffffU;
    PixelSpan(caster).row(0)[1] = 0x80808080U;
    PixelSpan(caster).row(0)[2] = 0x40404040U;
    QImage canvas = makeCanonicalImage(QSize(5, 3), 1.0);

    QVERIFY(paintShadow(canvas, caster, QPoint(1, 1), shadowOf(0.5, 0, 0)));
    QCOMPARE(pixelAt(canvas, 1, 1), 0x80000000U);
    QCOMPARE(pixelAt(canvas, 2, 1), 0x40000000U);
    QCOMPARE(pixelAt(canvas, 3, 1), 0x20000000U);
    QCOMPARE(pixelAt(canvas, 0, 1), 0U);
    QCOMPARE(pixelAt(canvas, 1, 0), 0U);
}

void ShadowTest::aShadowIsOffsetDownwardAndBlurredWithoutLosingMass() {
    QImage canvas = makeCanonicalImage(QSize(160, 160), 1.0);
    const QImage caster = filledImage(QSize(40, 40), kOpaqueWhite);

    QVERIFY(paintShadow(canvas, caster, QPoint(60, 50), shadowOf(0.5, 4.35, 3)));

    const Mass mass = massOf(canvas);
    QVERIFY2(std::abs(mass.alpha - 128.0 * 40 * 40) < 0.005 * 128.0 * 40 * 40, qPrintable(QString::number(mass.alpha)));
    QVERIFY2(std::abs(mass.centreX - 80.0) < 0.01, qPrintable(QString::number(mass.centreX)));
    QVERIFY2(std::abs(mass.centreY - (70.0 + 4.35)) < 0.02, qPrintable(QString::number(mass.centreY)));
    QCOMPARE(alphaAt(canvas, 80, 75), 128);
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            QVERIFY(isBlack(pixelAt(canvas, x, y)));
        }
    }
    QCOMPARE(pixelAt(canvas, 5, 5), 0U);
    QCOMPARE(pixelAt(canvas, 80, 150), 0U);
}

void ShadowTest::aFractionalOffsetMixesTwoRows() {
    // The caster covers rows 10 to 29. Moved down by 2.5 rows it covers 12.5 to 32.5: the rows at either end are half
    // covered.
    QImage canvas = makeCanonicalImage(QSize(40, 50), 1.0);
    QVERIFY(paintShadow(canvas, filledImage(QSize(20, 20), kOpaqueWhite), QPoint(10, 10), shadowOf(1, 2.5, 0)));

    QCOMPARE(pixelAt(canvas, 15, 11), 0U);
    QCOMPARE(pixelAt(canvas, 15, 12), 0x80000000U);
    QCOMPARE(pixelAt(canvas, 15, 13), kOpaqueBlack);
    QCOMPARE(pixelAt(canvas, 15, 31), kOpaqueBlack);
    QCOMPARE(pixelAt(canvas, 15, 32), 0x80000000U);
    QCOMPARE(pixelAt(canvas, 15, 33), 0U);
}

void ShadowTest::offsetAndBlurAreInPointsOfTheCanvasScale() {
    // On a canvas of scale 2, an offset of 4 points is 8 pixels.
    QImage canvas = makeCanonicalImage(QSize(80, 80), 2.0);
    QVERIFY(paintShadow(canvas, filledImage(QSize(20, 20), kOpaqueWhite, 2.0), QPoint(10, 10), shadowOf(1, 4, 0)));
    QCOMPARE(pixelAt(canvas, 15, 17), 0U);
    QCOMPARE(pixelAt(canvas, 15, 18), kOpaqueBlack);
    QCOMPARE(pixelAt(canvas, 15, 37), kOpaqueBlack);
    QCOMPARE(pixelAt(canvas, 15, 38), 0U);

    // A blur of 3 points is a sigma of 6 pixels: the same shadow is twice as wide as on a canvas of scale 1.
    QImage scaleOne = makeCanonicalImage(QSize(160, 160), 1.0);
    QImage scaleTwo = makeCanonicalImage(QSize(160, 160), 2.0);
    QVERIFY(paintShadow(scaleOne, filledImage(QSize(40, 40), kOpaqueWhite), QPoint(60, 60), shadowOf(1, 0, 3)));
    QVERIFY(paintShadow(scaleTwo, filledImage(QSize(40, 40), kOpaqueWhite, 2.0), QPoint(60, 60), shadowOf(1, 0, 3)));
    const auto extentOf = [](const QImage& image) {
        int first = image.width();
        int last = -1;
        for (int x = 0; x < image.width(); ++x) {
            if (alphaAt(image, x, 80) != 0) {
                first = std::min(first, x);
                last = std::max(last, x);
            }
        }
        return last - first + 1;
    };
    QVERIFY(extentOf(scaleTwo) > extentOf(scaleOne) + 10);
}

void ShadowTest::aShadowRunningOffTheCanvasMatchesTheSameShadowOnALargerCanvas() {
    // The shadow of a caster 6 pixels from the corner spreads about 30 pixels past it; the small canvas shows the part
    // of the large canvas that starts 50 pixels in.
    const QImage caster = filledImage(QSize(40, 40), kOpaqueWhite);
    const ShadowParameters shadow = shadowOf(0.8, 5.5, 10);
    QImage large = makeCanonicalImage(QSize(200, 200), 1.0);
    QImage small = makeCanonicalImage(QSize(100, 100), 1.0);
    QVERIFY(paintShadow(large, caster, QPoint(56, 56), shadow));
    QVERIFY(paintShadow(small, caster, QPoint(6, 6), shadow));

    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            QVERIFY2(pixelAt(small, x, y) == pixelAt(large, x + 50, y + 50),
                     qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
        }
    }
    // The shadow really does run off the left edge of the small canvas.
    QVERIFY(alphaAt(small, 0, 25) != 0);
}

void ShadowTest::refusesUnusableInput_data() {
    QTest::addColumn<QImage>("canvas");
    QTest::addColumn<QImage>("caster");
    QTest::addColumn<ShadowParameters>("shadow");

    const QImage canvas = makeCanonicalImage(QSize(20, 20), 1.0);
    const QImage caster = filledImage(QSize(4, 4), kOpaqueWhite);
    QImage plain(QSize(4, 4), QImage::Format_RGB32);
    plain.fill(Qt::white);
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    QTest::newRow("null canvas") << QImage() << caster << shadowOf(0.5, 1, 1);
    QTest::newRow("null caster") << canvas << QImage() << shadowOf(0.5, 1, 1);
    QTest::newRow("caster not canonical") << canvas << plain << shadowOf(0.5, 1, 1);
    QTest::newRow("opacity above 1") << canvas << caster << shadowOf(1.5, 1, 1);
    QTest::newRow("negative opacity") << canvas << caster << shadowOf(-0.1, 1, 1);
    QTest::newRow("opacity not a number") << canvas << caster << shadowOf(kNaN, 1, 1);
    QTest::newRow("negative offset") << canvas << caster << shadowOf(0.5, -1, 1);
    QTest::newRow("negative blur") << canvas << caster << shadowOf(0.5, 1, -1);
    QTest::newRow("offset beyond a million points") << canvas << caster << shadowOf(0.5, 1.0e7, 1);
    QTest::newRow("blur beyond a million points") << canvas << caster << shadowOf(0.5, 1, 1.0e7);
    QTest::newRow("blur not a number") << canvas << caster << shadowOf(0.5, 1, kNaN);
}

void ShadowTest::refusesUnusableInput() {
    QFETCH(QImage, canvas);
    QFETCH(QImage, caster);
    QFETCH(ShadowParameters, shadow);

    const QImage before = canvas.copy();
    QVERIFY(!paintShadow(canvas, caster, QPoint(2, 2), shadow));
    QCOMPARE(canvas, before);
}

void ShadowTest::roundedModePaintsAmbientThenContactThenTheCrispImage() {
    // A translucent caster makes the order visible: each pass draws over what the earlier ones left.
    const QImage caster = filledImage(QSize(60, 60), kTranslucentWhite);
    QImage canvas = makeCanonicalImage(QSize(200, 200), 1.0);
    QVERIFY(paintRoundedImageWithShadow(canvas, caster, QPoint(70, 60), 10));

    QImage expected = makeCanonicalImage(QSize(200, 200), 1.0);
    paintSequence(expected, caster, QPoint(70, 60), ambientShadow(10), contactShadow(10), 1);
    QImage reversed = makeCanonicalImage(QSize(200, 200), 1.0);
    paintSequence(reversed, caster, QPoint(70, 60), contactShadow(10), ambientShadow(10), 1);

    QCOMPARE(canvas, expected);
    QVERIFY(canvas != reversed);
}

void ShadowTest::snappedWindowPaintsContactThenAmbientThenTheCrispImage() {
    const QImage caster = filledImage(QSize(60, 60), kTranslucentWhite);
    QImage canvas = makeCanonicalImage(QSize(200, 200), 1.0);
    QVERIFY(paintSnappedWindowShadow(canvas, caster, QPoint(70, 60), 10));

    QImage expected = makeCanonicalImage(QSize(200, 200), 1.0);
    paintSequence(expected, caster, QPoint(70, 60), contactShadow(10), ambientShadow(10), 1);
    QImage reversed = makeCanonicalImage(QSize(200, 200), 1.0);
    paintSequence(reversed, caster, QPoint(70, 60), ambientShadow(10), contactShadow(10), 1);

    QCOMPARE(canvas, expected);
    QVERIFY(canvas != reversed);
}

void ShadowTest::windowBodyPaintsContactThenAmbientUnderAnOpaqueBody() {
    // With square corners the body is an opaque white rectangle; the window itself is drawn later by the caller.
    const QRect body(70, 60, 60, 40);
    QImage canvas = makeCanonicalImage(QSize(200, 200), 1.0);
    QVERIFY(paintWindowBodyShadow(canvas, body, 0, 10));

    const QImage bodyImage = filledImage(body.size(), kOpaqueWhite);
    QImage expected = makeCanonicalImage(QSize(200, 200), 1.0);
    QVERIFY(paintShadow(expected, bodyImage, body.topLeft(), contactShadow(10)));
    drawOver(expected, bodyImage, body.topLeft());
    QVERIFY(paintShadow(expected, bodyImage, body.topLeft(), ambientShadow(10)));
    drawOver(expected, bodyImage, body.topLeft());

    QCOMPARE(canvas, expected);
    QCOMPARE(pixelAt(canvas, 100, 80), kOpaqueWhite);
}

void ShadowTest::windowBodyCornersFollowTheRadiusInPoints() {
    // A corner radius of 5 points is 5 pixels on a canvas of scale 1 and 10 pixels on a canvas of scale 2. The pixel 2
    // from the corner lies inside the first arc and outside the second.
    const QRect body(40, 40, 60, 40);
    QImage scaleOne = makeCanonicalImage(QSize(160, 160), 1.0);
    QImage scaleTwo = makeCanonicalImage(QSize(160, 160), 2.0);
    QVERIFY(paintWindowBodyShadow(scaleOne, body, 5, 10));
    QVERIFY(paintWindowBodyShadow(scaleTwo, body, 5, 10));

    QCOMPARE(pixelAt(scaleOne, 42, 42), kOpaqueWhite);
    QVERIFY(pixelAt(scaleTwo, 42, 42) != kOpaqueWhite);
    QCOMPARE(pixelAt(scaleOne, 70, 60), kOpaqueWhite);
    QCOMPARE(pixelAt(scaleTwo, 70, 60), kOpaqueWhite);
}

void ShadowTest::aShadowRadiusOfZeroPaintsNoShadow() {
    const QImage caster = filledImage(QSize(10, 10), kTranslucentWhite);
    QImage rounded = makeCanonicalImage(QSize(40, 40), 1.0);
    QImage snapped = makeCanonicalImage(QSize(40, 40), 1.0);
    QImage body = makeCanonicalImage(QSize(40, 40), 1.0);
    QVERIFY(paintRoundedImageWithShadow(rounded, caster, QPoint(10, 10), 0));
    QVERIFY(paintSnappedWindowShadow(snapped, caster, QPoint(10, 10), 0));
    QVERIFY(paintWindowBodyShadow(body, QRect(10, 10, 10, 10), 2, 0));

    // The images are drawn once, crisp; the window body is not drawn at all without a shadow.
    QImage expected = makeCanonicalImage(QSize(40, 40), 1.0);
    drawOver(expected, caster, QPoint(10, 10));
    QCOMPARE(rounded, expected);
    QCOMPARE(snapped, expected);
    QCOMPARE(body, makeCanonicalImage(QSize(40, 40), 1.0));
}

QTEST_GUILESS_MAIN(ShadowTest)
#include "tst_Shadow.moc"
