// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the expected step sizes follow macshot/Model/Annotation.swift:1990-2015@b4d4f3a (see PROVENANCE.md).

#include "render/CanonicalImage.h"
#include "render/effects/Pixelate.h"
#include "render/pixel/PixelSpan.h"

#include <QColorSpace>
#include <QTest>

#include <algorithm>
#include <limits>

using ariadshot::render::ConstPixelSpan;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::pixelate;
using ariadshot::render::PixelateSizes;
using ariadshot::render::pixelateSizes;
using ariadshot::render::PixelSpan;

namespace {

constexpr quint32 pixelOf(quint32 alpha, quint32 red, quint32 green, quint32 blue) {
    return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

constexpr quint32 grayOf(quint32 level) { return pixelOf(255U, level, level, level); }

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

// An image made of vertical bands of the given width, one entry of `bands` after the other from the left.
QImage bandedImage(QSize size, int bandWidth, std::initializer_list<quint32> bands) {
    QImage image = makeCanonicalImage(size, 1.0);
    const PixelSpan view(image);
    for (int y = 0; y < view.height(); ++y) {
        int start = 0;
        for (const quint32 band : bands) {
            std::ranges::fill(view.row(y).subspan(toIndex(start), toIndex(bandWidth)), band);
            start += bandWidth;
        }
    }
    return image;
}

// Whether the columns [first, last] of every row hold exactly the pixel.
bool columnsHold(const QImage& image, int first, int last, quint32 pixel) {
    const ConstPixelSpan view(image);
    for (int y = 0; y < view.height(); ++y) {
        const auto row = view.row(y).subspan(toIndex(first), toIndex(last - first + 1));
        if (!std::ranges::all_of(row, [pixel](quint32 value) { return value == pixel; })) {
            return false;
        }
    }
    return true;
}

} // namespace

class PixelateTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void stepSizesFollowTheThreeSteps_data();
    void stepSizesFollowTheThreeSteps();
    void alignedBlocksBecomeCrispSquares();
    void averagesPartiallyCoveredPixelsByArea();
    void keepsAUniformRegionUniform();
    void producesACanonicalImageOfTwiceThePointSize();
    void rejectsUnusableInput_data();
    void rejectsUnusableInput();
};

void PixelateTest::stepSizesFollowTheThreeSteps_data() {
    QTest::addColumn<QSize>("regionPixelSize");
    QTest::addColumn<QSizeF>("regionPointSize");
    QTest::addColumn<QSize>("first");
    QTest::addColumn<QSize>("second");
    QTest::addColumn<QSize>("output");

    // first = max(1, pixels / 8), second = max(1, first / 2), output = max(1, Int(points * 2)), all with integer
    // division and truncation: macshot/Model/Annotation.swift:1990-2011@b4d4f3a
    QTest::newRow("aligned region") << QSize(64, 48) << QSizeF(32, 24) << QSize(8, 6) << QSize(4, 3) << QSize(64, 48);
    QTest::newRow("region smaller than a block")
        << QSize(10, 7) << QSizeF(5, 3.5) << QSize(1, 1) << QSize(1, 1) << QSize(10, 7);
    QTest::newRow("two blocks") << QSize(20, 20) << QSizeF(10, 10) << QSize(2, 2) << QSize(1, 1) << QSize(20, 20);
    QTest::newRow("odd block counts") << QSize(100, 60) << QSizeF(50, 30) << QSize(12, 7) << QSize(6, 3)
                                      << QSize(100, 60);
    QTest::newRow("scale 2 capture") << QSize(200, 100) << QSizeF(100, 50) << QSize(25, 12) << QSize(12, 6)
                                     << QSize(200, 100);
    QTest::newRow("scale 1 capture") << QSize(100, 60) << QSizeF(100, 60) << QSize(12, 7) << QSize(6, 3)
                                     << QSize(200, 120);
    QTest::newRow("fractional points truncate")
        << QSize(123, 45) << QSizeF(61.5, 22.25) << QSize(15, 5) << QSize(7, 2) << QSize(123, 44);
    QTest::newRow("less than half a point")
        << QSize(1, 1) << QSizeF(0.4, 0.4) << QSize(1, 1) << QSize(1, 1) << QSize(1, 1);
    QTest::newRow("large region") << QSize(3000, 2000) << QSizeF(1500, 1000) << QSize(375, 250) << QSize(187, 125)
                                  << QSize(3000, 2000);
}

void PixelateTest::stepSizesFollowTheThreeSteps() {
    QFETCH(QSize, regionPixelSize);
    QFETCH(QSizeF, regionPointSize);
    QFETCH(QSize, first);
    QFETCH(QSize, second);
    QFETCH(QSize, output);

    const PixelateSizes sizes = pixelateSizes(regionPixelSize, regionPointSize);
    QCOMPARE(sizes.first, first);
    QCOMPARE(sizes.second, second);
    QCOMPARE(sizes.output, output);
}

void PixelateTest::alignedBlocksBecomeCrispSquares() {
    // 32 x 16 pixels step down to 4 x 2 and then 2 x 1, which are exactly the two halves; they come back as blocks.
    constexpr quint32 kRed = pixelOf(255, 255, 0, 0);
    constexpr quint32 kBlue = pixelOf(255, 0, 0, 255);
    const QImage region = bandedImage(QSize(32, 16), 16, {kRed, kBlue});

    const QImage result = pixelate(region, QSizeF(32, 16));
    QCOMPARE(result.size(), QSize(64, 32));
    QVERIFY(columnsHold(result, 0, 31, kRed));
    QVERIFY(columnsHold(result, 32, 63, kBlue));
}

void PixelateTest::averagesPartiallyCoveredPixelsByArea() {
    // 40 x 8 pixels step down to 5 x 1, one pixel per band of 8, and then to 2 x 1. The first of those covers bands 0
    // and 1 and half of band 2, the second the other half of band 2 and bands 3 and 4:
    // (0 + 40 + 80 / 2) / 2.5 = 32 and (80 / 2 + 120 + 200) / 2.5 = 144.
    const QImage region = bandedImage(QSize(40, 8), 8, {grayOf(0), grayOf(40), grayOf(80), grayOf(120), grayOf(200)});
    const QImage before = region.copy();

    const QImage result = pixelate(region, QSizeF(20, 4));
    QCOMPARE(region, before);
    QCOMPARE(result.size(), QSize(40, 8));
    QVERIFY(columnsHold(result, 0, 19, grayOf(32)));
    QVERIFY(columnsHold(result, 20, 39, grayOf(144)));
}

void PixelateTest::keepsAUniformRegionUniform() {
    const quint32 pixel = pixelOf(128, 64, 32, 16);
    const QImage result = pixelate(bandedImage(QSize(50, 30), 50, {pixel}), QSizeF(25, 15));
    QCOMPARE(result.size(), QSize(50, 30));
    QVERIFY(columnsHold(result, 0, 49, pixel));
}

void PixelateTest::producesACanonicalImageOfTwiceThePointSize() {
    const QImage result = pixelate(bandedImage(QSize(100, 60), 100, {grayOf(77)}), QSizeF(100, 60));
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), QSize(200, 120));
    QCOMPARE(result.devicePixelRatio(), 2.0);
    QCOMPARE(result.deviceIndependentSize(), QSizeF(100, 60));
    QCOMPARE(result.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(result.colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void PixelateTest::rejectsUnusableInput_data() {
    QTest::addColumn<QImage>("region");
    QTest::addColumn<QSizeF>("regionPointSize");

    const QImage canonical = makeCanonicalImage(QSize(16, 16), 1.0);
    QImage opaque(QSize(16, 16), QImage::Format_RGB32);
    opaque.fill(Qt::black);
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInfinity = std::numeric_limits<double>::infinity();

    QTest::newRow("null image") << QImage() << QSizeF(8, 8);
    QTest::newRow("not the canonical format") << opaque << QSizeF(8, 8);
    QTest::newRow("zero width") << canonical << QSizeF(0, 8);
    QTest::newRow("negative height") << canonical << QSizeF(8, -1);
    QTest::newRow("width not a number") << canonical << QSizeF(kNaN, 8);
    QTest::newRow("height infinite") << canonical << QSizeF(8, kInfinity);
}

void PixelateTest::rejectsUnusableInput() {
    QFETCH(QImage, region);
    QFETCH(QSizeF, regionPointSize);

    QVERIFY(pixelate(region, regionPointSize).isNull());
}

QTEST_GUILESS_MAIN(PixelateTest)
#include "tst_Pixelate.moc"
