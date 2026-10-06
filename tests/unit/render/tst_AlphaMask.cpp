// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/pixel/AlphaMask.h"
#include "render/pixel/PixelSpan.h"

#include <QColorSpace>
#include <QTest>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <limits>

using ariadshot::render::compositeOver;
using ariadshot::render::compositeShadowMask;
using ariadshot::render::ConstPixelSpan;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::makeShadowMask;
using ariadshot::render::PixelSpan;

namespace {

constexpr quint32 kOpaqueWhite = 0xffffffffU;

constexpr quint32 pixelOf(quint32 alpha, quint32 red, quint32 green, quint32 blue) {
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

QImage filledImage(QSize size, quint32 pixel) {
    QImage image = makeCanonicalImage(size, 1.0);
    const PixelSpan view(image);
    for (int y = 0; y < view.height(); ++y) {
        std::ranges::fill(view.row(y), pixel);
    }
    return image;
}

quint32 pixelAt(const QImage& image, int x, int y) {
    const quint32* pixel = ConstPixelSpan(image).pixel(x, y);
    return pixel != nullptr ? *pixel : 0xdeadbeefU;
}

void setPixel(QImage& image, int x, int y, quint32 value) { *PixelSpan(image).pixel(x, y) = value; }

QImage notCanonical() {
    QImage image(QSize(4, 4), QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}

// The alpha, in 0..255, of each canvas pixel as text, row by row, for readable failures.
QString describe(const QImage& image) {
    QString text;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            text += QStringLiteral("%1 ").arg(pixelAt(image, x, y) >> 24, 3);
        }
        text += QLatin1Char('\n');
    }
    return text;
}

} // namespace

class AlphaMaskTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void compositeOverBlendsPremultipliedPixels();
    void compositeOverDrawsOnlyWhatLiesOnTheCanvas_data();
    void compositeOverDrawsOnlyWhatLiesOnTheCanvas();
    void compositeOverRefusesUnusableImages();
    void aShadowMaskHoldsTheCastersAlphaScaledByTheOpacityOnBlack();
    void aShadowMaskHasRoomForTheMarginAndTheMove();
    void aShadowMaskRefusesUnusableInput_data();
    void aShadowMaskRefusesUnusableInput();
    void aMaskIsCompositedAsBlackWithItsAlpha();
    void aMaskMovesDownByWholeAndFractionalRows_data();
    void aMaskMovesDownByWholeAndFractionalRows();
    void aMaskIsClippedToTheCanvas();
    void aMaskPositionedThroughTheLimitsOfAnIntLandsExactly();
    void aMaskFarOutsideTheCanvasPaintsNothing_data();
    void aMaskFarOutsideTheCanvasPaintsNothing();
    void compositeShadowMaskRefusesUnusableInput_data();
    void compositeShadowMaskRefusesUnusableInput();
};

void AlphaMaskTest::compositeOverBlendsPremultipliedPixels() {
    // Premultiplied source-over: each channel is source + destination * (255 - source alpha) / 255, rounded to nearest.
    // 200 + (100 * 55 + 127) / 255 = 222, 100 + (40 * 55 + 127) / 255 = 109, 50 + (60 * 55 + 127) / 255 = 63 and
    // 0 + (20 * 55 + 127) / 255 = 4.
    QImage canvas = filledImage(QSize(4, 1), pixelOf(100, 40, 60, 20));
    QImage image = makeCanonicalImage(QSize(4, 1), 1.0);
    setPixel(image, 0, 0, pixelOf(200, 100, 50, 0));
    setPixel(image, 1, 0, kOpaqueWhite);
    setPixel(image, 2, 0, 0U);
    // A destination channel of 127 under a source of alpha 254 keeps 127 / 255 = 0.498 of itself, which rounds down to
    // 0; the alpha channel keeps 200 / 255 of itself and 254 + (200 + 127) / 255 = 255.
    setPixel(canvas, 3, 0, pixelOf(200, 127, 127, 127));
    setPixel(image, 3, 0, pixelOf(254, 0, 0, 0));

    QVERIFY(compositeOver(canvas, image, QPoint(0, 0)));
    QCOMPARE(pixelAt(canvas, 0, 0), pixelOf(222, 109, 63, 4));
    QCOMPARE(pixelAt(canvas, 1, 0), kOpaqueWhite);
    QCOMPARE(pixelAt(canvas, 2, 0), pixelOf(100, 40, 60, 20));
    QCOMPARE(pixelAt(canvas, 3, 0), pixelOf(255, 0, 0, 0));
}

void AlphaMaskTest::compositeOverDrawsOnlyWhatLiesOnTheCanvas_data() {
    QTest::addColumn<QPoint>("origin");

    QTest::newRow("inside") << QPoint(1, 1);
    QTest::newRow("cut by the left edge") << QPoint(-1, 1);
    QTest::newRow("cut by the right edge") << QPoint(4, 1);
    QTest::newRow("cut by the top edge") << QPoint(1, -1);
    QTest::newRow("cut by the bottom edge") << QPoint(1, 4);
    QTest::newRow("cut by the top left corner") << QPoint(-2, -1);
    QTest::newRow("cut by the bottom right corner") << QPoint(5, 4);
    QTest::newRow("covering the whole canvas") << QPoint(0, 0);
    QTest::newRow("just left of the canvas") << QPoint(-3, 1);
    QTest::newRow("just right of the canvas") << QPoint(6, 1);
    QTest::newRow("just above the canvas") << QPoint(1, -2);
    QTest::newRow("just below the canvas") << QPoint(1, 5);
    QTest::newRow("smallest x") << QPoint(INT_MIN, 1);
    QTest::newRow("largest x") << QPoint(INT_MAX, 1);
    QTest::newRow("smallest y") << QPoint(1, INT_MIN);
    QTest::newRow("largest y") << QPoint(1, INT_MAX);
    QTest::newRow("smallest x and y") << QPoint(INT_MIN, INT_MIN);
    QTest::newRow("largest x and y") << QPoint(INT_MAX, INT_MAX);
    QTest::newRow("one before the smallest x that could reach the canvas") << QPoint(INT_MIN + 1, 1);
    QTest::newRow("one short of the largest x") << QPoint(INT_MAX - 1, 1);
}

void AlphaMaskTest::compositeOverDrawsOnlyWhatLiesOnTheCanvas() {
    QFETCH(QPoint, origin);

    // The image is 3 pixels wide and 2 high on a canvas of 6 by 5. A canvas pixel is painted exactly when the image
    // covers it; the position is worked out in 64 bits so the test itself cannot overflow.
    constexpr int kImageWidth = 3;
    constexpr int kImageHeight = 2;
    QImage canvas = makeCanonicalImage(QSize(6, 5), 1.0);
    QVERIFY(compositeOver(canvas, filledImage(QSize(kImageWidth, kImageHeight), kOpaqueWhite), origin));

    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            const std::int64_t column = std::int64_t{x} - origin.x();
            const std::int64_t row = std::int64_t{y} - origin.y();
            const bool covered = column >= 0 && column < kImageWidth && row >= 0 && row < kImageHeight;
            QVERIFY2(pixelAt(canvas, x, y) == (covered ? kOpaqueWhite : 0U),
                     qPrintable(QStringLiteral("pixel %1,%2\n%3").arg(x).arg(y).arg(describe(canvas))));
        }
    }
}

void AlphaMaskTest::compositeOverRefusesUnusableImages() {
    const QImage canvasBefore = filledImage(QSize(4, 4), pixelOf(10, 1, 2, 3));
    const QImage image = filledImage(QSize(2, 2), kOpaqueWhite);

    QImage canvas = canvasBefore;
    QVERIFY(!compositeOver(canvas, QImage(), QPoint(0, 0)));
    QVERIFY(!compositeOver(canvas, notCanonical(), QPoint(0, 0)));
    QCOMPARE(canvas, canvasBefore);

    QImage null;
    QVERIFY(!compositeOver(null, image, QPoint(0, 0)));
    QImage plain = notCanonical();
    QVERIFY(!compositeOver(plain, image, QPoint(0, 0)));
}

void AlphaMaskTest::aShadowMaskHoldsTheCastersAlphaScaledByTheOpacityOnBlack() {
    // The caster has alpha 255, 128 and 64 in three columns, in colours that must not reach the mask. With an opacity
    // of 0.5 the mask has alpha 127.5 -> 128, 64 and 32, rounded to nearest.
    QImage caster = makeCanonicalImage(QSize(3, 1), 1.0);
    setPixel(caster, 0, 0, 0xffffffffU);
    setPixel(caster, 1, 0, 0x80808080U);
    setPixel(caster, 2, 0, 0x40404040U);

    const QImage mask = makeShadowMask(caster, 0.5, 2, 3);
    QVERIFY(!mask.isNull());
    QCOMPARE(mask.size(), QSize(7, 8));
    QCOMPARE(pixelAt(mask, 2, 2), 0x80000000U);
    QCOMPARE(pixelAt(mask, 3, 2), 0x40000000U);
    QCOMPARE(pixelAt(mask, 4, 2), 0x20000000U);
    for (int y = 0; y < mask.height(); ++y) {
        for (int x = 0; x < mask.width(); ++x) {
            const bool onCaster = y == 2 && x >= 2 && x <= 4;
            QVERIFY2(onCaster || pixelAt(mask, x, y) == 0U, qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
        }
    }

    const QImage opaque = makeShadowMask(caster, 1.0, 0, 0);
    QCOMPARE(pixelAt(opaque, 0, 0), 0xff000000U);
    QCOMPARE(pixelAt(opaque, 1, 0), 0x80000000U);
    QCOMPARE(pixelAt(opaque, 2, 0), 0x40000000U);
    const QImage invisible = makeShadowMask(caster, 0.0, 0, 0);
    QCOMPARE(invisible, makeCanonicalImage(QSize(3, 1), 1.0));
}

void AlphaMaskTest::aShadowMaskHasRoomForTheMarginAndTheMove() {
    QImage caster = filledImage(QSize(5, 4), kOpaqueWhite);
    caster.setDevicePixelRatio(2.0);

    const QImage bare = makeShadowMask(caster, 1.0, 0, 0);
    QCOMPARE(bare.size(), QSize(5, 4));
    const QImage grown = makeShadowMask(caster, 1.0, 6, 10);
    QCOMPARE(grown.size(), QSize(5 + 12, 4 + 12 + 10));
    // The mask is a canvas of its own, at scale 1, whatever the caster's scale.
    QCOMPARE(grown.devicePixelRatio(), 1.0);
    QCOMPARE(grown.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(grown.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(pixelAt(grown, 6, 6), 0xff000000U);
    QCOMPARE(pixelAt(grown, 10, 9), 0xff000000U);
    QCOMPARE(pixelAt(grown, 11, 6), 0U);
    QCOMPARE(pixelAt(grown, 6, 10), 0U);
}

void AlphaMaskTest::aShadowMaskRefusesUnusableInput_data() {
    QTest::addColumn<QImage>("caster");
    QTest::addColumn<double>("opacity");
    QTest::addColumn<int>("margin");
    QTest::addColumn<int>("extraRows");

    const QImage caster = filledImage(QSize(4, 4), kOpaqueWhite);
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    QTest::newRow("null caster") << QImage() << 0.5 << 1 << 1;
    QTest::newRow("caster not canonical") << notCanonical() << 0.5 << 1 << 1;
    QTest::newRow("negative opacity") << caster << -0.1 << 1 << 1;
    QTest::newRow("opacity above 1") << caster << 1.1 << 1 << 1;
    QTest::newRow("opacity not a number") << caster << kNaN << 1 << 1;
    QTest::newRow("negative margin") << caster << 0.5 << -1 << 1;
    QTest::newRow("smallest margin") << caster << 0.5 << INT_MIN << 1;
    QTest::newRow("negative extra rows") << caster << 0.5 << 1 << -1;
    QTest::newRow("smallest extra rows") << caster << 0.5 << 1 << INT_MIN;
    // The widths and heights are summed in 64 bits and refused above the largest int before anything is allocated.
    QTest::newRow("margin that overflows the width") << caster << 0.5 << INT_MAX << 0;
    QTest::newRow("margin of half an int") << caster << 0.5 << (INT_MAX / 2) + 1 << 0;
    QTest::newRow("extra rows that overflow the height") << caster << 0.5 << 0 << INT_MAX;
}

void AlphaMaskTest::aShadowMaskRefusesUnusableInput() {
    QFETCH(QImage, caster);
    QFETCH(double, opacity);
    QFETCH(int, margin);
    QFETCH(int, extraRows);

    QVERIFY(makeShadowMask(caster, opacity, margin, extraRows).isNull());
}

void AlphaMaskTest::aMaskIsCompositedAsBlackWithItsAlpha() {
    // A 5 by 5 mask with margin 2 around a caster of one pixel: its single non-zero pixel is the caster's position.
    QImage mask = makeCanonicalImage(QSize(5, 5), 1.0);
    setPixel(mask, 2, 2, 0xc8000000U);
    QImage canvas = makeCanonicalImage(QSize(9, 9), 1.0);

    QVERIFY(compositeShadowMask(canvas, mask, QPoint(4, 4), 2, 0.0));
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            QVERIFY2(pixelAt(canvas, x, y) == (x == 4 && y == 4 ? 0xc8000000U : 0U),
                     qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
        }
    }

    // Over a coloured canvas the shadow is black source-over: each channel keeps (255 - 200) / 255 of its value.
    QImage coloured = filledImage(QSize(9, 9), kOpaqueWhite);
    QVERIFY(compositeShadowMask(coloured, mask, QPoint(4, 4), 2, 0.0));
    QCOMPARE(pixelAt(coloured, 4, 4), pixelOf(255, 55, 55, 55));
    QCOMPARE(pixelAt(coloured, 3, 4), kOpaqueWhite);
}

void AlphaMaskTest::aMaskMovesDownByWholeAndFractionalRows_data() {
    QTest::addColumn<double>("rowOffset");
    QTest::addColumn<int>("firstRow");
    QTest::addColumn<int>("firstAlpha");
    QTest::addColumn<int>("secondAlpha");

    // The mask's one non-zero pixel has alpha 200 and sits at the caster, on canvas row 4. Moved down by w whole rows
    // and a fraction f, a canvas row takes (1 - f) of the mask row w above it and f of the row w + 1 above it, rounded.
    QTest::newRow("no move") << 0.0 << 4 << 200 << 0;
    QTest::newRow("two rows") << 2.0 << 6 << 200 << 0;
    QTest::newRow("half a row") << 0.5 << 4 << 100 << 100;
    QTest::newRow("a quarter more than a row") << 1.25 << 5 << 150 << 50;
    QTest::newRow("three quarters of a row") << 0.75 << 4 << 50 << 150;
    QTest::newRow("two and a tenth") << 2.1 << 6 << 180 << 20;
}

void AlphaMaskTest::aMaskMovesDownByWholeAndFractionalRows() {
    QFETCH(double, rowOffset);
    QFETCH(int, firstRow);
    QFETCH(int, firstAlpha);
    QFETCH(int, secondAlpha);

    // The layout of makeShadowMask for a caster of one pixel, a margin of 2 and 3 extra rows to move down into.
    QImage mask = makeCanonicalImage(QSize(5, 8), 1.0);
    setPixel(mask, 2, 2, 0xc8000000U);
    QImage canvas = makeCanonicalImage(QSize(9, 12), 1.0);

    QVERIFY(compositeShadowMask(canvas, mask, QPoint(4, 4), 2, rowOffset));
    for (int y = 0; y < canvas.height(); ++y) {
        int expected = 0;
        if (y == firstRow) {
            expected = firstAlpha;
        } else if (y == firstRow + 1) {
            expected = secondAlpha;
        }
        QVERIFY2(
            pixelAt(canvas, 4, y) == static_cast<quint32>(expected) << 24,
            qPrintable(
                QStringLiteral("row %1 is %2\n%3").arg(y).arg(pixelAt(canvas, 4, y), 8, 16).arg(describe(canvas))));
        QVERIFY2(pixelAt(canvas, 3, y) == 0U && pixelAt(canvas, 5, y) == 0U,
                 qPrintable(QStringLiteral("row %1").arg(y)));
    }
}

void AlphaMaskTest::aMaskIsClippedToTheCanvas() {
    // A mask of 5 by 5 pixels, every pixel of alpha 100, with margin 2: its top-left pixel is 2 up and left of the
    // caster. With the caster at (-1, -1) it covers canvas columns -3 to 1 and rows -3 to 1, so a 4 by 4 canvas shows
    // the pixels at columns 0 and 1 of rows 0 and 1 and nothing else.
    const QImage mask = filledImage(QSize(5, 5), 0x64000000U);
    QImage canvas = makeCanonicalImage(QSize(4, 4), 1.0);
    QVERIFY(compositeShadowMask(canvas, mask, QPoint(-1, -1), 2, 0.0));
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            QVERIFY2(pixelAt(canvas, x, y) == (x <= 1 && y <= 1 ? 0x64000000U : 0U),
                     qPrintable(QStringLiteral("pixel %1,%2\n%3").arg(x).arg(y).arg(describe(canvas))));
        }
    }

    // The same at the far corner: the caster at (4, 4) puts the mask at columns 2 to 6.
    QImage corner = makeCanonicalImage(QSize(4, 4), 1.0);
    QVERIFY(compositeShadowMask(corner, mask, QPoint(4, 4), 2, 0.0));
    for (int y = 0; y < corner.height(); ++y) {
        for (int x = 0; x < corner.width(); ++x) {
            QVERIFY2(pixelAt(corner, x, y) == (x >= 2 && y >= 2 ? 0x64000000U : 0U),
                     qPrintable(QStringLiteral("pixel %1,%2\n%3").arg(x).arg(y).arg(describe(corner))));
        }
    }
}

void AlphaMaskTest::aMaskPositionedThroughTheLimitsOfAnIntLandsExactly() {
    // The mask's top-left is the caster's position minus the margin; with the caster at the largest int and a margin
    // close to it the mask lands a few pixels from the canvas origin, and the position is exact.
    const QImage mask = filledImage(QSize(5, 5), 0x64000000U);
    for (const int shortBy : {0, 1, 3}) {
        QImage canvas = makeCanonicalImage(QSize(4, 4), 1.0);
        QVERIFY(compositeShadowMask(canvas, mask, QPoint(INT_MAX, INT_MAX), INT_MAX - shortBy, 0.0));
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < canvas.width(); ++x) {
                QVERIFY2(pixelAt(canvas, x, y) == (x >= shortBy && y >= shortBy ? 0x64000000U : 0U),
                         qPrintable(QStringLiteral("pixel %1,%2\n%3").arg(x).arg(y).arg(describe(canvas))));
            }
        }
    }
}

void AlphaMaskTest::aMaskFarOutsideTheCanvasPaintsNothing_data() {
    QTest::addColumn<QPoint>("casterOrigin");
    QTest::addColumn<int>("margin");
    QTest::addColumn<double>("rowOffset");

    QTest::newRow("smallest x") << QPoint(INT_MIN, 2) << 2 << 0.0;
    QTest::newRow("largest x") << QPoint(INT_MAX, 2) << 2 << 0.0;
    QTest::newRow("smallest y") << QPoint(2, INT_MIN) << 2 << 0.0;
    QTest::newRow("largest y") << QPoint(2, INT_MAX) << 2 << 0.0;
    QTest::newRow("smallest x and y") << QPoint(INT_MIN, INT_MIN) << 2 << 0.0;
    QTest::newRow("largest x and y") << QPoint(INT_MAX, INT_MAX) << 2 << 0.0;
    // The mask's own position is the caster's minus the margin, which must not overflow either.
    QTest::newRow("smallest origin and the largest margin") << QPoint(INT_MIN, INT_MIN) << INT_MAX << 0.0;
    QTest::newRow("a margin that puts the mask beyond the smallest int") << QPoint(INT_MIN + 1, 2) << INT_MAX << 0.0;
    QTest::newRow("smallest origin and a large move") << QPoint(INT_MIN, INT_MIN) << 2 << 1.0e9;
    QTest::newRow("a move beyond the canvas") << QPoint(2, 2) << 2 << 1000.5;
    QTest::newRow("a move of the largest int") << QPoint(2, 2) << 2 << 2147483647.0;
    QTest::newRow("largest y and the largest move") << QPoint(2, INT_MAX) << 2 << 2147483647.0;
}

void AlphaMaskTest::aMaskFarOutsideTheCanvasPaintsNothing() {
    QFETCH(QPoint, casterOrigin);
    QFETCH(int, margin);
    QFETCH(double, rowOffset);

    const QImage mask = filledImage(QSize(5, 5), 0xff000000U);
    QImage canvas = makeCanonicalImage(QSize(6, 6), 1.0);
    QVERIFY(compositeShadowMask(canvas, mask, casterOrigin, margin, rowOffset));
    QCOMPARE(canvas, makeCanonicalImage(QSize(6, 6), 1.0));
}

void AlphaMaskTest::compositeShadowMaskRefusesUnusableInput_data() {
    QTest::addColumn<QImage>("canvas");
    QTest::addColumn<QImage>("mask");
    QTest::addColumn<int>("margin");
    QTest::addColumn<double>("rowOffset");

    const QImage canvas = makeCanonicalImage(QSize(8, 8), 1.0);
    const QImage mask = filledImage(QSize(3, 3), 0xff000000U);
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInfinity = std::numeric_limits<double>::infinity();

    QTest::newRow("null canvas") << QImage() << mask << 1 << 0.0;
    QTest::newRow("canvas not canonical") << notCanonical() << mask << 1 << 0.0;
    QTest::newRow("null mask") << canvas << QImage() << 1 << 0.0;
    QTest::newRow("mask not canonical") << canvas << notCanonical() << 1 << 0.0;
    QTest::newRow("negative margin") << canvas << mask << -1 << 0.0;
    QTest::newRow("smallest margin") << canvas << mask << INT_MIN << 0.0;
    QTest::newRow("negative move") << canvas << mask << 1 << -0.5;
    QTest::newRow("move not a number") << canvas << mask << 1 << kNaN;
    QTest::newRow("infinite move") << canvas << mask << 1 << kInfinity;
    QTest::newRow("move above the largest int") << canvas << mask << 1 << 2147483648.0;
    QTest::newRow("very large move") << canvas << mask << 1 << 1.0e300;
}

void AlphaMaskTest::compositeShadowMaskRefusesUnusableInput() {
    QFETCH(QImage, canvas);
    QFETCH(QImage, mask);
    QFETCH(int, margin);
    QFETCH(double, rowOffset);

    const QImage before = canvas.copy();
    QVERIFY(!compositeShadowMask(canvas, mask, QPoint(3, 3), margin, rowOffset));
    QCOMPARE(canvas, before);
}

QTEST_GUILESS_MAIN(AlphaMaskTest)
#include "tst_AlphaMask.moc"
