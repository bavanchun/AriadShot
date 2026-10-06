// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/pixel/BoxBlur.h"
#include "render/pixel/PixelSpan.h"

#include <QColorSpace>
#include <QTest>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <vector>

using ariadshot::render::boxBlur;
using ariadshot::render::ConstPixelSpan;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::PixelSpan;

namespace {

using Radii = std::array<int, 3>;
using Levels = std::vector<int>;

constexpr quint32 pixelOf(quint32 alpha, quint32 red, quint32 green, quint32 blue) {
    return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

// Channel 0 is alpha, then red, green and blue.
int channelOf(quint32 pixel, int channel) {
    return static_cast<int>((pixel >> static_cast<unsigned int>(24 - 8 * channel)) & 0xffU);
}

std::size_t toIndex(int value) { return static_cast<std::size_t>(value); }

QImage constantImage(QSize size, quint32 pixel) {
    QImage image = makeCanonicalImage(size, 1.0);
    const PixelSpan view(image);
    for (int y = 0; y < view.height(); ++y) {
        std::ranges::fill(view.row(y), pixel);
    }
    return image;
}

// Deterministic pseudo-random premultiplied pixels: the colour channels never exceed alpha.
QImage randomImage(QSize size) {
    QImage image = makeCanonicalImage(size, 1.0);
    const PixelSpan view(image);
    quint32 state = 4321;
    const auto next = [&state](quint32 limit) {
        state = state * 1664525U + 1013904223U;
        return (state >> 16U) % (limit + 1U);
    };
    for (int y = 0; y < view.height(); ++y) {
        for (quint32& pixel : view.row(y)) {
            const quint32 alpha = next(255);
            pixel = pixelOf(alpha, next(alpha), next(alpha), next(alpha));
        }
    }
    return image;
}

// The mean of the 2r + 1 neighbours of every element, with the ends repeated: a box pass by its definition.
std::vector<double> boxPassOf(const std::vector<double>& line, int radius) {
    const int count = static_cast<int>(line.size());
    std::vector<double> result(line.size());
    for (int i = 0; i < count; ++i) {
        double sum = 0.0;
        for (int j = i - radius; j <= i + radius; ++j) {
            sum += line[toIndex(std::clamp(j, 0, count - 1))];
        }
        result[toIndex(i)] = sum / (2 * radius + 1);
    }
    return result;
}

std::vector<double> passesOf(std::vector<double> line, const Radii& radii) {
    for (const int radius : radii) {
        line = boxPassOf(line, radius);
    }
    return line;
}

// The largest difference, over every channel of every pixel, between the blurred image and the same blur computed by
// direct summation: the three passes over the rows, then over the columns.
int largestDifferenceFromDirectSummation(const QImage& blurred, const QImage& source, const Radii& radii) {
    const ConstPixelSpan in(source);
    const ConstPixelSpan out(blurred);
    const int width = in.width();
    const int height = in.height();
    int largest = 0;
    for (int channel = 0; channel < 4; ++channel) {
        std::vector<std::vector<double>> rows(toIndex(height), std::vector<double>(toIndex(width)));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                rows[toIndex(y)][toIndex(x)] = channelOf(in.row(y)[toIndex(x)], channel);
            }
            rows[toIndex(y)] = passesOf(rows[toIndex(y)], radii);
        }
        for (int x = 0; x < width; ++x) {
            std::vector<double> column(toIndex(height));
            for (int y = 0; y < height; ++y) {
                column[toIndex(y)] = rows[toIndex(y)][toIndex(x)];
            }
            column = passesOf(column, radii);
            for (int y = 0; y < height; ++y) {
                const int actual = channelOf(out.row(y)[toIndex(x)], channel);
                largest = std::max(largest, std::abs(actual - static_cast<int>(std::lround(column[toIndex(y)]))));
            }
        }
    }
    return largest;
}

} // namespace

class BoxBlurTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void radiiOfZeroKeepEveryPixel();
    void spreadsAnImpulseIntoTheProductOfTheBoxes_data();
    void spreadsAnImpulseIntoTheProductOfTheBoxes();
    void aConstantImageStaysConstantWhateverTheRadii_data();
    void aConstantImageStaysConstantWhateverTheRadii();
    void matchesDirectSummationOnARandomImage_data();
    void matchesDirectSummationOnARandomImage();
    void keepsPremultipliedPixelsValid();
    void keepsSizeScaleFormatAndColourSpaceAndLeavesTheSourceAlone();
    void rejectsUnusableInput_data();
    void rejectsUnusableInput();
};

void BoxBlurTest::radiiOfZeroKeepEveryPixel() {
    const QImage source = randomImage(QSize(12, 9));
    const QImage blurred = boxBlur(source, Radii{0, 0, 0});
    QVERIFY(!blurred.isNull());
    QCOMPARE(blurred, source);
}

void BoxBlurTest::spreadsAnImpulseIntoTheProductOfTheBoxes_data() {
    QTest::addColumn<Radii>("radii");
    QTest::addColumn<Levels>("levels");

    // The kernel of three boxes of widths w1, w2 and w3 is the convolution of three uniform kernels. The impulse is a
    // single opaque white pixel of level 255, so the pixel at distance d from it ends with round(255 * kernel[d]). The
    // image is one pixel high, which leaves the column passes with nothing to spread. Entries run from the centre out.
    // Widths 3, 3, 3: [1 3 6 7 6 3 1] / 27.
    QTest::newRow("three boxes of width 3") << Radii{1, 1, 1} << Levels{66, 57, 28, 9};
    // Widths 5, 3, 1: [1 2 3 3 3 2 1] / 15.
    QTest::newRow("widths 5, 3 and 1") << Radii{2, 1, 0} << Levels{51, 51, 34, 17};
    // The order of the boxes does not change the interior of the result.
    QTest::newRow("widths 1, 3 and 5") << Radii{0, 1, 2} << Levels{51, 51, 34, 17};
    // One box of width 7: 255 / 7 everywhere in the window.
    QTest::newRow("one box of width 7") << Radii{0, 0, 3} << Levels{36, 36, 36, 36};
}

void BoxBlurTest::spreadsAnImpulseIntoTheProductOfTheBoxes() {
    QFETCH(Radii, radii);
    QFETCH(Levels, levels);

    constexpr int kCentre = 8;
    QImage source = makeCanonicalImage(QSize(2 * kCentre + 1, 1), 1.0);
    PixelSpan(source).row(0)[toIndex(kCentre)] = pixelOf(255, 255, 255, 255);

    const QImage blurred = boxBlur(source, radii);
    QVERIFY(!blurred.isNull());
    const ConstPixelSpan view(blurred);
    for (int x = 0; x < view.width(); ++x) {
        const std::size_t distance = toIndex(std::abs(x - kCentre));
        const quint32 level = distance < levels.size() ? static_cast<quint32>(levels[distance]) : 0U;
        QVERIFY2(view.row(0)[toIndex(x)] == pixelOf(level, level, level, level),
                 qPrintable(QStringLiteral("column %1 is %2").arg(x).arg(view.row(0)[toIndex(x)], 8, 16)));
    }
}

void BoxBlurTest::aConstantImageStaysConstantWhateverTheRadii_data() {
    QTest::addColumn<QSize>("size");
    QTest::addColumn<Radii>("radii");

    QTest::newRow("radii below the extent") << QSize(9, 7) << Radii{1, 2, 3};
    QTest::newRow("radii beyond the extent") << QSize(5, 4) << Radii{3, 40, 1000};
    QTest::newRow("a single pixel") << QSize(1, 1) << Radii{4, 0, 2};
    // A radius this large must not overflow the index arithmetic.
    QTest::newRow("radii at the limit of an int") << QSize(5, 4) << Radii{INT_MAX, INT_MAX - 1, INT_MAX};
    QTest::newRow("a single pixel, radii at the limit") << QSize(1, 1) << Radii{INT_MAX, INT_MAX, INT_MAX};
}

void BoxBlurTest::aConstantImageStaysConstantWhateverTheRadii() {
    QFETCH(QSize, size);
    QFETCH(Radii, radii);

    const quint32 pixel = pixelOf(200, 100, 50, 20);
    const QImage blurred = boxBlur(constantImage(size, pixel), radii);
    QVERIFY(!blurred.isNull());
    const ConstPixelSpan view(blurred);
    for (int y = 0; y < view.height(); ++y) {
        for (int x = 0; x < view.width(); ++x) {
            QVERIFY2(view.row(y)[toIndex(x)] == pixel, qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
        }
    }
}

void BoxBlurTest::matchesDirectSummationOnARandomImage_data() {
    QTest::addColumn<QSize>("size");
    QTest::addColumn<Radii>("radii");

    QTest::newRow("radii 2, 3 and 1") << QSize(21, 15) << Radii{2, 3, 1};
    QTest::newRow("radii 1, 0 and 4") << QSize(21, 15) << Radii{1, 0, 4};
    QTest::newRow("radii beyond a three pixel image") << QSize(3, 3) << Radii{2, 5, 1};
    QTest::newRow("single column") << QSize(1, 9) << Radii{2, 1, 3};
    QTest::newRow("single row") << QSize(9, 1) << Radii{2, 1, 3};
}

void BoxBlurTest::matchesDirectSummationOnARandomImage() {
    QFETCH(QSize, size);
    QFETCH(Radii, radii);

    const QImage source = randomImage(size);
    const QImage blurred = boxBlur(source, radii);
    QVERIFY(!blurred.isNull());
    QVERIFY(largestDifferenceFromDirectSummation(blurred, source, radii) <= 1);
}

void BoxBlurTest::keepsPremultipliedPixelsValid() {
    const QImage blurred = boxBlur(randomImage(QSize(40, 30)), Radii{3, 2, 4});
    QVERIFY(!blurred.isNull());
    const ConstPixelSpan view(blurred);
    for (int y = 0; y < view.height(); ++y) {
        for (const quint32 pixel : view.row(y)) {
            const int alpha = channelOf(pixel, 0);
            QVERIFY(channelOf(pixel, 1) <= alpha && channelOf(pixel, 2) <= alpha && channelOf(pixel, 3) <= alpha);
        }
    }
}

void BoxBlurTest::keepsSizeScaleFormatAndColourSpaceAndLeavesTheSourceAlone() {
    QImage source = randomImage(QSize(9, 7));
    source.setDevicePixelRatio(2.0);
    const QImage before = source.copy();

    const QImage blurred = boxBlur(source, Radii{1, 1, 1});
    QVERIFY(!blurred.isNull());
    QCOMPARE(blurred.size(), QSize(9, 7));
    QCOMPARE(blurred.devicePixelRatio(), 2.0);
    QCOMPARE(blurred.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(blurred.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(source, before);
}

void BoxBlurTest::rejectsUnusableInput_data() {
    QTest::addColumn<QImage>("source");
    QTest::addColumn<Radii>("radii");

    const QImage canonical = constantImage(QSize(4, 4), pixelOf(255, 9, 9, 9));
    QImage opaque(QSize(4, 4), QImage::Format_RGB32);
    opaque.fill(Qt::black);

    QTest::newRow("null image") << QImage() << Radii{1, 1, 1};
    QTest::newRow("not the canonical format") << opaque << Radii{1, 1, 1};
    QTest::newRow("negative first radius") << canonical << Radii{-1, 1, 1};
    QTest::newRow("negative second radius") << canonical << Radii{1, -1, 1};
    QTest::newRow("negative third radius") << canonical << Radii{1, 1, INT_MIN};
}

void BoxBlurTest::rejectsUnusableInput() {
    QFETCH(QImage, source);
    QFETCH(Radii, radii);

    QVERIFY(boxBlur(source, radii).isNull());
}

QTEST_GUILESS_MAIN(BoxBlurTest)
#include "tst_BoxBlur.moc"
