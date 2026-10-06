// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the expected censor blur sigmas follow macshot/Model/Annotation.swift:2256@b4d4f3a (see PROVENANCE.md).

#include "render/CanonicalImage.h"
#include "render/effects/Blur.h"
#include "render/pixel/PixelSpan.h"

#include <QColorSpace>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using ariadshot::render::boxBlurWidths;
using ariadshot::render::censorBlurSigma;
using ariadshot::render::ConstPixelSpan;
using ariadshot::render::gaussianBlur;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::PixelSpan;

namespace {

using Widths = std::array<int, 3>;

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
    quint32 state = 12345;
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
std::vector<double> boxPass(const std::vector<double>& line, int radius) {
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

std::vector<double> threeBoxPasses(std::vector<double> line, double sigma) {
    for (const int width : boxBlurWidths(sigma)) {
        line = boxPass(line, (width - 1) / 2);
    }
    return line;
}

// The largest difference, over every channel of every pixel, between the blurred image and the same blur computed by
// direct summation: three box passes over the rows, then over the columns.
int largestDifferenceFromDirectSummation(const QImage& blurred, const QImage& source, double sigma) {
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
            rows[toIndex(y)] = threeBoxPasses(rows[toIndex(y)], sigma);
        }
        for (int x = 0; x < width; ++x) {
            std::vector<double> column(toIndex(height));
            for (int y = 0; y < height; ++y) {
                column[toIndex(y)] = rows[toIndex(y)][toIndex(x)];
            }
            column = threeBoxPasses(column, sigma);
            for (int y = 0; y < height; ++y) {
                const int actual = channelOf(out.row(y)[toIndex(x)], channel);
                largest = std::max(largest, std::abs(actual - static_cast<int>(std::lround(column[toIndex(y)]))));
            }
        }
    }
    return largest;
}

} // namespace

class BlurTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void censorSigmaIsTheLargerOfTenAndThreePercentOfTheShorterSide_data();
    void censorSigmaIsTheLargerOfTenAndThreePercentOfTheShorterSide();
    void boxWidthsFollowTheThreeBoxConstruction_data();
    void boxWidthsFollowTheThreeBoxConstruction();
    void aConstantImageStaysConstantIncludingItsEdges_data();
    void aConstantImageStaysConstantIncludingItsEdges();
    void matchesDirectSummationOfThreeBoxPasses_data();
    void matchesDirectSummationOfThreeBoxPasses();
    void spreadsAnImpulseIntoTheSymmetricThreeBoxKernel();
    void approximatesAGaussianOfTheRequestedSigma();
    void keepsPremultipliedPixelsValid();
    void keepsSizeScaleFormatAndColourSpace();
    void aZeroSigmaKeepsEveryPixel();
    void rejectsUnusableInput_data();
    void rejectsUnusableInput();
};

void BlurTest::censorSigmaIsTheLargerOfTenAndThreePercentOfTheShorterSide_data() {
    QTest::addColumn<QSize>("regionPixelSize");
    QTest::addColumn<double>("sigma");

    // max(10.0, min(w, h) * 0.03), macshot/Model/Annotation.swift:2256@b4d4f3a
    QTest::newRow("tiny region") << QSize(1, 1) << 10.0;
    QTest::newRow("shorter side 100") << QSize(100, 200) << 10.0;
    QTest::newRow("just below the floor") << QSize(333, 1000) << 10.0;
    QTest::newRow("just above the floor") << QSize(334, 5000) << 10.02;
    QTest::newRow("shorter side 500") << QSize(1000, 500) << 15.0;
    QTest::newRow("shorter side 1000") << QSize(2000, 1000) << 30.0;
    QTest::newRow("shorter side 3000") << QSize(4000, 3000) << 90.0;
}

void BlurTest::censorSigmaIsTheLargerOfTenAndThreePercentOfTheShorterSide() {
    QFETCH(QSize, regionPixelSize);
    QFETCH(double, sigma);

    QCOMPARE(censorBlurSigma(regionPixelSize), sigma);
}

void BlurTest::boxWidthsFollowTheThreeBoxConstruction_data() {
    QTest::addColumn<double>("sigma");
    QTest::addColumn<Widths>("widths");

    // wIdeal = sqrt(12 sigma^2 / 3 + 1), wl the odd integer at or below it, wu = wl + 2, and
    // m = round((12 sigma^2 - 3 wl^2 - 12 wl - 9) / (-4 wl - 4)) boxes of width wl, the rest wu.
    QTest::newRow("sigma 1") << 1.0 << Widths{1, 1, 3};
    QTest::newRow("sigma 1.5") << 1.5 << Widths{3, 3, 3};
    QTest::newRow("sigma 2") << 2.0 << Widths{3, 3, 5};
    QTest::newRow("sigma 3") << 3.0 << Widths{5, 5, 7};
    QTest::newRow("sigma 5") << 5.0 << Widths{9, 9, 11};
    QTest::newRow("sigma 6") << 6.0 << Widths{11, 11, 13};
    QTest::newRow("sigma 10") << 10.0 << Widths{19, 19, 21};
    QTest::newRow("sigma 15") << 15.0 << Widths{29, 29, 31};
    QTest::newRow("sigma 30") << 30.0 << Widths{59, 59, 61};
    QTest::newRow("sigma 90") << 90.0 << Widths{179, 179, 181};
    // Large enough that the squares of the widths no longer fit an int.
    QTest::newRow("sigma 20000") << 20000.0 << Widths{39999, 39999, 40001};
    QTest::newRow("largest accepted sigma") << 1.0e6 << Widths{1999999, 1999999, 2000001};
}

void BlurTest::boxWidthsFollowTheThreeBoxConstruction() {
    QFETCH(double, sigma);
    QFETCH(Widths, widths);

    QCOMPARE(boxBlurWidths(sigma), widths);
}

void BlurTest::aConstantImageStaysConstantIncludingItsEdges_data() {
    QTest::addColumn<double>("sigma");

    QTest::newRow("small sigma") << 0.8;
    QTest::newRow("sigma below the image size") << 3.0;
    QTest::newRow("sigma beyond the image size") << 40.0;
    QTest::newRow("largest accepted sigma") << 1.0e6;
}

void BlurTest::aConstantImageStaysConstantIncludingItsEdges() {
    QFETCH(double, sigma);

    const quint32 pixel = pixelOf(200, 100, 50, 20);
    const QImage blurred = gaussianBlur(constantImage(QSize(17, 13), pixel), sigma);
    QVERIFY(!blurred.isNull());
    const ConstPixelSpan view(blurred);
    for (int y = 0; y < view.height(); ++y) {
        for (int x = 0; x < view.width(); ++x) {
            QVERIFY2(view.row(y)[toIndex(x)] == pixel, qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
        }
    }
}

void BlurTest::matchesDirectSummationOfThreeBoxPasses_data() {
    QTest::addColumn<QSize>("size");
    QTest::addColumn<double>("sigma");

    QTest::newRow("small sigma") << QSize(31, 23) << 1.0;
    QTest::newRow("medium sigma") << QSize(31, 23) << 2.5;
    QTest::newRow("sigma near the image size") << QSize(31, 23) << 7.0;
    QTest::newRow("radius beyond a three pixel image") << QSize(3, 3) << 2.5;
    QTest::newRow("single column") << QSize(1, 9) << 2.5;
    QTest::newRow("single row") << QSize(9, 1) << 2.5;
}

void BlurTest::matchesDirectSummationOfThreeBoxPasses() {
    QFETCH(QSize, size);
    QFETCH(double, sigma);

    const QImage source = randomImage(size);
    const QImage before = source.copy();
    const QImage blurred = gaussianBlur(source, sigma);
    QVERIFY(!blurred.isNull());
    QCOMPARE(source, before);
    QVERIFY(largestDifferenceFromDirectSummation(blurred, source, sigma) <= 1);
}

void BlurTest::spreadsAnImpulseIntoTheSymmetricThreeBoxKernel() {
    // Sigma 1.5 uses three boxes of width 3; three boxes of width 3 convolve to [1 3 6 7 6 3 1] / 27 on each axis.
    constexpr std::array<int, 7> kKernel = {1, 3, 6, 7, 6, 3, 1};
    QImage source = makeCanonicalImage(QSize(21, 21), 1.0);
    PixelSpan(source).row(10)[10] = pixelOf(255, 255, 255, 255);

    const QImage blurred = gaussianBlur(source, 1.5);
    QVERIFY(!blurred.isNull());
    const ConstPixelSpan view(blurred);
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const int dx = x - 10;
            const int dy = y - 10;
            quint32 expected = 0;
            if (std::abs(dx) <= 3 && std::abs(dy) <= 3) {
                const double weight = kKernel[toIndex(dx + 3)] * kKernel[toIndex(dy + 3)] / 729.0;
                expected = static_cast<quint32>(std::lround(255.0 * weight));
            }
            const quint32 pixel = view.row(y)[toIndex(x)];
            QVERIFY2(pixel == pixelOf(expected, expected, expected, expected),
                     qPrintable(QStringLiteral("pixel %1,%2 is %3").arg(x).arg(y).arg(pixel, 8, 16)));
        }
    }
}

void BlurTest::approximatesAGaussianOfTheRequestedSigma() {
    constexpr double kSigma = 6.0;
    constexpr int kEdge = 60;
    QImage source = makeCanonicalImage(QSize(120, 8), 1.0);
    const PixelSpan view(source);
    for (int y = 0; y < view.height(); ++y) {
        std::ranges::fill(view.row(y).first(kEdge), pixelOf(255, 255, 255, 255));
    }

    const QImage blurred = gaussianBlur(source, kSigma);
    QVERIFY(!blurred.isNull());
    // A step edge blurs into 255 * Phi((edge - (x + 0.5)) / sigma); three boxes stay within a few levels of it.
    for (int x = 0; x < 120; ++x) {
        const double expected = 255.0 * 0.5 * std::erfc((x + 0.5 - kEdge) / (kSigma * std::numbers::sqrt2));
        const int alpha = channelOf(ConstPixelSpan(blurred).row(4)[toIndex(x)], 0);
        QVERIFY2(std::abs(alpha - expected) <= 3.0, qPrintable(QStringLiteral("column %1: %2").arg(x).arg(alpha)));
    }
}

void BlurTest::keepsPremultipliedPixelsValid() {
    const QImage blurred = gaussianBlur(randomImage(QSize(40, 30)), 3.0);
    QVERIFY(!blurred.isNull());
    const ConstPixelSpan view(blurred);
    for (int y = 0; y < view.height(); ++y) {
        for (const quint32 pixel : view.row(y)) {
            const int alpha = channelOf(pixel, 0);
            QVERIFY(channelOf(pixel, 1) <= alpha && channelOf(pixel, 2) <= alpha && channelOf(pixel, 3) <= alpha);
        }
    }
}

void BlurTest::keepsSizeScaleFormatAndColourSpace() {
    QImage source = constantImage(QSize(9, 7), pixelOf(255, 1, 2, 3));
    source.setDevicePixelRatio(2.0);

    const QImage blurred = gaussianBlur(source, 2.0);
    QVERIFY(!blurred.isNull());
    QCOMPARE(blurred.size(), QSize(9, 7));
    QCOMPARE(blurred.devicePixelRatio(), 2.0);
    QCOMPARE(blurred.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(blurred.colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void BlurTest::aZeroSigmaKeepsEveryPixel() {
    const QImage source = randomImage(QSize(12, 9));
    const QImage blurred = gaussianBlur(source, 0.0);
    QVERIFY(!blurred.isNull());
    QCOMPARE(blurred, source);
}

void BlurTest::rejectsUnusableInput_data() {
    QTest::addColumn<QImage>("source");
    QTest::addColumn<double>("sigma");

    const QImage canonical = constantImage(QSize(4, 4), pixelOf(255, 9, 9, 9));
    QImage opaque(QSize(4, 4), QImage::Format_RGB32);
    opaque.fill(Qt::black);

    QTest::newRow("null image") << QImage() << 2.0;
    QTest::newRow("not the canonical format") << opaque << 2.0;
    QTest::newRow("negative sigma") << canonical << -1.0;
    QTest::newRow("sigma not a number") << canonical << std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("infinite sigma") << canonical << std::numeric_limits<double>::infinity();
    QTest::newRow("sigma beyond any image") << canonical << 1.0e9;
}

void BlurTest::rejectsUnusableInput() {
    QFETCH(QImage, source);
    QFETCH(double, sigma);

    QVERIFY(gaussianBlur(source, sigma).isNull());
}

QTEST_GUILESS_MAIN(BlurTest)
#include "tst_Blur.moc"
