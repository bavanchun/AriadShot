// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "support/ImageCompare.h"

#include <QColorSpace>
#include <QTest>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;
using ariadshot::tests::ImageCompare;
using ariadshot::tests::Lab;

class ImageCompareTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void ciede2000MatchesPublishedData_data();
    void ciede2000MatchesPublishedData();
    void ciede2000IsSymmetric();
    void labConversionOfReferenceColours_data();
    void labConversionOfReferenceColours();
    void exactAcceptsTheSameColoursInAnotherFormat();
    void exactRejectsOnePixelDifference();
    void exactRejectsDifferentDevicePixelRatio();
    void exactRejectsDifferentSize();
    void perceptualPassesInvisiblePixels();
    void perceptualFailsAlphaDifferenceAboveOne();
    void perceptualComparesColourWhenAlphaDiffersByOne();
    void perceptualAppliesThresholdAndFraction();
    void perceptualIgnoresMaskedPixels();
};

namespace {

QImage filled(QSize size, QColor colour, QImage::Format format = QImage::Format_ARGB32_Premultiplied) {
    QImage image(size, format);
    image.fill(colour);
    return image;
}

} // namespace

// The 34 colour pairs of Sharma, Wu and Dalal, "The CIEDE2000 Color-Difference Formula: Implementation Notes,
// Supplementary Test Data, and Mathematical Observations", Color Research and Application 30(1), 2005, as published in
// ciede2000testdata.txt at https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/ (values rounded to 4 decimals).
void ImageCompareTest::ciede2000MatchesPublishedData_data() {
    QTest::addColumn<double>("l1");
    QTest::addColumn<double>("a1");
    QTest::addColumn<double>("b1");
    QTest::addColumn<double>("l2");
    QTest::addColumn<double>("a2");
    QTest::addColumn<double>("b2");
    QTest::addColumn<double>("expected");

    // L1, a1, b1, L2, a2, b2, ΔE00.
    static constexpr std::array<std::array<double, 7>, 34> kPairs{{
        {{50.0000, 2.6772, -79.7751, 50.0000, 0.0000, -82.7485, 2.0425}},
        {{50.0000, 3.1571, -77.2803, 50.0000, 0.0000, -82.7485, 2.8615}},
        {{50.0000, 2.8361, -74.0200, 50.0000, 0.0000, -82.7485, 3.4412}},
        {{50.0000, -1.3802, -84.2814, 50.0000, 0.0000, -82.7485, 1.0000}},
        {{50.0000, -1.1848, -84.8006, 50.0000, 0.0000, -82.7485, 1.0000}},
        {{50.0000, -0.9009, -85.5211, 50.0000, 0.0000, -82.7485, 1.0000}},
        {{50.0000, 0.0000, 0.0000, 50.0000, -1.0000, 2.0000, 2.3669}},
        {{50.0000, -1.0000, 2.0000, 50.0000, 0.0000, 0.0000, 2.3669}},
        {{50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0009, 7.1792}},
        {{50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0010, 7.1792}},
        {{50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0011, 7.2195}},
        {{50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0012, 7.2195}},
        {{50.0000, -0.0010, 2.4900, 50.0000, 0.0009, -2.4900, 4.8045}},
        {{50.0000, -0.0010, 2.4900, 50.0000, 0.0010, -2.4900, 4.8045}},
        {{50.0000, -0.0010, 2.4900, 50.0000, 0.0011, -2.4900, 4.7461}},
        {{50.0000, 2.5000, 0.0000, 50.0000, 0.0000, -2.5000, 4.3065}},
        {{50.0000, 2.5000, 0.0000, 73.0000, 25.0000, -18.0000, 27.1492}},
        {{50.0000, 2.5000, 0.0000, 61.0000, -5.0000, 29.0000, 22.8977}},
        {{50.0000, 2.5000, 0.0000, 56.0000, -27.0000, -3.0000, 31.9030}},
        {{50.0000, 2.5000, 0.0000, 58.0000, 24.0000, 15.0000, 19.4535}},
        {{50.0000, 2.5000, 0.0000, 50.0000, 3.1736, 0.5854, 1.0000}},
        {{50.0000, 2.5000, 0.0000, 50.0000, 3.2972, 0.0000, 1.0000}},
        {{50.0000, 2.5000, 0.0000, 50.0000, 1.8634, 0.5757, 1.0000}},
        {{50.0000, 2.5000, 0.0000, 50.0000, 3.2592, 0.3350, 1.0000}},
        {{60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644}},
        {{63.0109, -31.0961, -5.8663, 62.8187, -29.7946, -4.0864, 1.2630}},
        {{61.2901, 3.7196, -5.3901, 61.4292, 2.2480, -4.9620, 1.8731}},
        {{35.0831, -44.1164, 3.7933, 35.0232, -40.0716, 1.5901, 1.8645}},
        {{22.7233, 20.0904, -46.6940, 23.0331, 14.9730, -42.5619, 2.0373}},
        // NOLINTNEXTLINE(modernize-use-std-numbers): published data that happens to be close to the square root of 2.
        {{36.4612, 47.8580, 18.3852, 36.2715, 50.5065, 21.2231, 1.4146}},
        {{90.8027, -2.0831, 1.4410, 91.1528, -1.6435, 0.0447, 1.4441}},
        {{90.9257, -0.5406, -0.9208, 88.6381, -0.8985, -0.7239, 1.5381}},
        {{6.7747, -0.2908, -2.4247, 5.8714, -0.0985, -2.2286, 0.6377}},
        {{2.0776, 0.0795, -1.1350, 0.9033, -0.0636, -0.5514, 0.9082}},
    }};
    int index = 1;
    for (const auto& pair : kPairs) {
        QTest::addRow("pair %d", index++) << pair[0] << pair[1] << pair[2] << pair[3] << pair[4] << pair[5] << pair[6];
    }
}

void ImageCompareTest::ciede2000MatchesPublishedData() {
    QFETCH(double, l1);
    QFETCH(double, a1);
    QFETCH(double, b1);
    QFETCH(double, l2);
    QFETCH(double, a2);
    QFETCH(double, b2);
    QFETCH(double, expected);

    const double actual = ImageCompare::ciede2000(Lab{.l = l1, .a = a1, .b = b1}, Lab{.l = l2, .a = a2, .b = b2});
    QVERIFY2(std::abs(actual - expected) <= 1e-4,
             qPrintable(u"ΔE00 %1, expected %2"_s.arg(actual, 0, 'f', 6).arg(expected, 0, 'f', 4)));
}

void ImageCompareTest::ciede2000IsSymmetric() {
    const Lab first{.l = 60.2574, .a = -34.0099, .b = 36.2677};
    const Lab second{.l = 60.4626, .a = -34.1751, .b = 39.4387};
    QCOMPARE(ImageCompare::ciede2000(first, second), ImageCompare::ciede2000(second, first));
    QCOMPARE(ImageCompare::ciede2000(first, first), 0.0);
}

void ImageCompareTest::labConversionOfReferenceColours_data() {
    QTest::addColumn<QRgb>("colour");
    QTest::addColumn<double>("l");
    QTest::addColumn<double>("a");
    QTest::addColumn<double>("b");

    // The sRGB primaries with the IEC 61966-2-1 matrix and its D65 white (0.9505, 1.0000, 1.0890), computed by hand
    // from the formulas. Tables built from the unrounded matrix differ by up to 0.03 (red a = 80.09, for example).
    QTest::newRow("white") << qRgb(255, 255, 255) << 100.0 << 0.0 << 0.0;
    QTest::newRow("black") << qRgb(0, 0, 0) << 0.0 << 0.0 << 0.0;
    QTest::newRow("red") << qRgb(255, 0, 0) << 53.2329 << 80.1053 << 67.2228;
    QTest::newRow("green") << qRgb(0, 255, 0) << 87.7370 << -86.1884 << 83.1861;
    QTest::newRow("blue") << qRgb(0, 0, 255) << 32.3026 << 79.1936 << -107.8537;
}

void ImageCompareTest::labConversionOfReferenceColours() {
    QFETCH(QRgb, colour);
    QFETCH(double, l);
    QFETCH(double, a);
    QFETCH(double, b);

    const Lab lab = ImageCompare::toLab(colour);
    const double tolerance = 1e-3;
    QVERIFY2(std::abs(lab.l - l) <= tolerance, qPrintable(u"L %1"_s.arg(lab.l)));
    QVERIFY2(std::abs(lab.a - a) <= tolerance, qPrintable(u"a %1"_s.arg(lab.a)));
    QVERIFY2(std::abs(lab.b - b) <= tolerance, qPrintable(u"b %1"_s.arg(lab.b)));
}

void ImageCompareTest::exactAcceptsTheSameColoursInAnotherFormat() {
    const QColor translucent(10, 200, 30, 128);
    const QImage premultiplied = filled(QSize(8, 4), translucent);
    const QImage straight = premultiplied.convertToFormat(QImage::Format_ARGB32);
    const auto result = ImageCompare::compareExact(premultiplied, straight);
    QVERIFY2(result.passed, qPrintable(result.failure));
    QCOMPARE(result.passingPixels, qint64(32));
}

void ImageCompareTest::exactRejectsOnePixelDifference() {
    const QImage expected = filled(QSize(8, 4), Qt::red);
    QImage actual = expected;
    actual.setPixel(3, 2, qRgba(254, 0, 0, 255));
    const auto result = ImageCompare::compareExact(actual, expected);
    QVERIFY(!result.passed);
    QCOMPARE(result.passingPixels, qint64(31));
    QCOMPARE(qAlpha(result.failureMask.pixel(3, 2)), 255);
    QCOMPARE(qAlpha(result.failureMask.pixel(0, 0)), 0);
}

void ImageCompareTest::exactRejectsDifferentDevicePixelRatio() {
    const QImage expected = filled(QSize(8, 4), Qt::red);
    QImage actual = expected;
    actual.setDevicePixelRatio(2.0);
    const auto result = ImageCompare::compareExact(actual, expected);
    QVERIFY(!result.passed);
    QVERIFY(result.failure.contains(u"device pixel ratio"_s));
}

void ImageCompareTest::exactRejectsDifferentSize() {
    const auto result = ImageCompare::compareExact(filled(QSize(8, 4), Qt::red), filled(QSize(4, 8), Qt::red));
    QVERIFY(!result.passed);
    QVERIFY(result.failure.contains(u"pixel size"_s));
}

void ImageCompareTest::perceptualPassesInvisiblePixels() {
    // Both alphas below 8/255: invisible, whatever the colour.
    const QImage actual = filled(QSize(4, 4), QColor(255, 0, 0, 7), QImage::Format_ARGB32);
    const QImage expected = filled(QSize(4, 4), QColor(0, 0, 255, 0), QImage::Format_ARGB32);
    const auto result =
        ImageCompare::comparePerceptual(actual, expected, {.maxDeltaE = 0.0, .minPassingFraction = 1.0});
    QVERIFY2(result.passed, qPrintable(result.failure));
}

void ImageCompareTest::perceptualFailsAlphaDifferenceAboveOne() {
    const QImage actual = filled(QSize(4, 4), QColor(10, 20, 30, 200), QImage::Format_ARGB32);
    const QImage expected = filled(QSize(4, 4), QColor(10, 20, 30, 202), QImage::Format_ARGB32);
    const auto result = ImageCompare::comparePerceptual(actual, expected, ImageCompare::kCorpus);
    QVERIFY(!result.passed);
    QCOMPARE(result.alphaMismatches, qint64(16));
}

void ImageCompareTest::perceptualComparesColourWhenAlphaDiffersByOne() {
    const QImage actual = filled(QSize(4, 4), QColor(10, 20, 30, 200), QImage::Format_ARGB32);
    const QImage expected = filled(QSize(4, 4), QColor(10, 20, 30, 201), QImage::Format_ARGB32);
    const auto result = ImageCompare::comparePerceptual(actual, expected, ImageCompare::kPresentation);
    QVERIFY2(result.passed, qPrintable(result.failure));
    QCOMPARE(result.alphaMismatches, qint64(0));
    QCOMPARE(result.maxDeltaE, 0.0);
}

void ImageCompareTest::perceptualAppliesThresholdAndFraction() {
    // 1000 pixels; one differs by a large colour step. 99.9 % pass, which meets the presentation threshold exactly.
    const QImage expected = filled(QSize(100, 10), QColor(120, 120, 120));
    QImage actual = expected;
    actual.setPixel(5, 5, qRgb(200, 40, 40));
    auto result = ImageCompare::comparePerceptual(actual, expected, ImageCompare::kPresentation);
    QVERIFY2(result.passed, qPrintable(result.failure));
    QCOMPARE(result.passingPixels, qint64(999));
    QVERIFY(result.maxDeltaE > 1.0);

    // A second differing pixel drops the fraction to 99.8 %.
    actual.setPixel(6, 5, qRgb(200, 40, 40));
    result = ImageCompare::comparePerceptual(actual, expected, ImageCompare::kPresentation);
    QVERIFY(!result.passed);
    QCOMPARE(result.passingPixels, qint64(998));
}

void ImageCompareTest::perceptualIgnoresMaskedPixels() {
    const QImage expected = filled(QSize(10, 10), QColor(0, 0, 0));
    QImage actual = expected;
    actual.setPixel(2, 2, qRgb(255, 255, 255));
    QImage mask(expected.size(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    mask.setPixel(2, 2, qRgba(255, 255, 255, 255));
    const auto result =
        ImageCompare::comparePerceptual(actual, expected, {.maxDeltaE = 0.0, .minPassingFraction = 1.0}, mask);
    QVERIFY2(result.passed, qPrintable(result.failure));
    QCOMPARE(result.comparedPixels, qint64(99));
}

QTEST_GUILESS_MAIN(ImageCompareTest)
#include "tst_ImageCompare.moc"
