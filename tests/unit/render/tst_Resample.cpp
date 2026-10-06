// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/pixel/PixelSpan.h"
#include "render/pixel/Resample.h"

#include <QColorSpace>
#include <QTest>

#include <algorithm>
#include <limits>
#include <vector>

using ariadshot::render::areaAverage;
using ariadshot::render::ConstPixelSpan;
using ariadshot::render::enlargeNearest;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::PixelSpan;

namespace {

using Pixels = std::vector<quint32>;

constexpr quint32 pixelOf(quint32 alpha, quint32 red, quint32 green, quint32 blue) {
    return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

constexpr quint32 grayOf(quint32 level) { return pixelOf(255U, level, level, level); }

// An image of the given size whose pixels are `pixels`, row by row.
QImage imageOf(QSize size, const Pixels& pixels) {
    QImage image = makeCanonicalImage(size, 1.0);
    const PixelSpan view(image);
    std::size_t next = 0;
    for (int y = 0; y < view.height(); ++y) {
        for (quint32& pixel : view.row(y)) {
            pixel = pixels.at(next++);
        }
    }
    return image;
}

Pixels pixelsOf(const QImage& image) {
    Pixels pixels;
    const ConstPixelSpan view(image);
    for (int y = 0; y < view.height(); ++y) {
        const auto row = view.row(y);
        pixels.insert(pixels.end(), row.begin(), row.end());
    }
    return pixels;
}

// Deterministic pseudo-random premultiplied pixels.
QImage randomImage(QSize size) {
    QImage image = makeCanonicalImage(size, 1.0);
    const PixelSpan view(image);
    quint32 state = 777;
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

} // namespace

class ResampleTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void areaAverageMeansTheCoveredPixels_data();
    void areaAverageMeansTheCoveredPixels();
    void areaAverageToTheSameSizeKeepsEveryPixel();
    void areaAverageProducesACanonicalImageOfScaleOne();
    void enlargeNearestSamplesAtPixelCentres_data();
    void enlargeNearestSamplesAtPixelCentres();
    void enlargeNearestSetsTheRequestedScale();
    void rejectsUnusableInput_data();
    void rejectsUnusableInput();
    void enlargeNearestRejectsAnUnusableScale_data();
    void enlargeNearestRejectsAnUnusableScale();
};

void ResampleTest::areaAverageMeansTheCoveredPixels_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<Pixels>("source");
    QTest::addColumn<QSize>("size");
    QTest::addColumn<Pixels>("expected");

    // Each destination pixel is the mean of the source area it covers, channel by channel, rounded to nearest with
    // halves going up.
    QTest::newRow("four pixels into one") << QSize(2, 2)
                                          << Pixels{pixelOf(255, 10, 20, 30), pixelOf(255, 30, 40, 50),
                                                    pixelOf(255, 50, 60, 70), pixelOf(255, 70, 80, 90)}
                                          << QSize(1, 1) << Pixels{pixelOf(255, 40, 50, 60)};
    QTest::newRow("alpha is averaged as a channel")
        << QSize(2, 1) << Pixels{pixelOf(200, 100, 50, 0), pixelOf(100, 0, 50, 100)} << QSize(1, 1)
        << Pixels{pixelOf(150, 50, 50, 50)};
    // 3 columns into 2: each destination pixel covers 1.5 source pixels, (30 + 0.5 * 60) / 1.5 and
    // (0.5 * 60 + 90) / 1.5.
    QTest::newRow("one and a half pixels per destination in a row")
        << QSize(3, 1) << Pixels{grayOf(30), grayOf(60), grayOf(90)} << QSize(2, 1) << Pixels{grayOf(40), grayOf(80)};
    QTest::newRow("one and a half pixels per destination in a column")
        << QSize(1, 3) << Pixels{grayOf(30), grayOf(60), grayOf(90)} << QSize(1, 2) << Pixels{grayOf(40), grayOf(80)};
    QTest::newRow("a half goes up") << QSize(2, 1) << Pixels{grayOf(1), grayOf(2)} << QSize(1, 1) << Pixels{grayOf(2)};
    QTest::newRow("a half of zero and one goes up")
        << QSize(2, 1) << Pixels{grayOf(0), grayOf(1)} << QSize(1, 1) << Pixels{grayOf(1)};
    QTest::newRow("both axes at once") << QSize(4, 4) << Pixels{grayOf(0),   grayOf(0),   grayOf(100), grayOf(100),
                                                                grayOf(0),   grayOf(0),   grayOf(100), grayOf(100),
                                                                grayOf(200), grayOf(200), grayOf(40),  grayOf(40),
                                                                grayOf(200), grayOf(200), grayOf(40),  grayOf(40)}
                                       << QSize(2, 2) << Pixels{grayOf(0), grayOf(100), grayOf(200), grayOf(40)};
}

void ResampleTest::areaAverageMeansTheCoveredPixels() {
    QFETCH(QSize, sourceSize);
    QFETCH(Pixels, source);
    QFETCH(QSize, size);
    QFETCH(Pixels, expected);

    const QImage result = areaAverage(imageOf(sourceSize, source), size);
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), size);
    QCOMPARE(pixelsOf(result), expected);
}

void ResampleTest::areaAverageToTheSameSizeKeepsEveryPixel() {
    const QImage source = randomImage(QSize(13, 7));
    const QImage result = areaAverage(source, source.size());
    QVERIFY(!result.isNull());
    QCOMPARE(result, source);
}

void ResampleTest::areaAverageProducesACanonicalImageOfScaleOne() {
    QImage source = randomImage(QSize(8, 8));
    source.setDevicePixelRatio(2.0);
    const QImage before = source.copy();

    const QImage result = areaAverage(source, QSize(4, 4));
    QVERIFY(!result.isNull());
    QCOMPARE(result.devicePixelRatio(), 1.0);
    QCOMPARE(result.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(result.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(source, before);
}

void ResampleTest::enlargeNearestSamplesAtPixelCentres_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<Pixels>("source");
    QTest::addColumn<QSize>("size");
    QTest::addColumn<Pixels>("expected");

    // Destination pixel d takes the source pixel containing its centre: floor((2 d + 1) * count / (2 * destination)).
    QTest::newRow("twice as wide") << QSize(2, 1) << Pixels{grayOf(10), grayOf(20)} << QSize(4, 1)
                                   << Pixels{grayOf(10), grayOf(10), grayOf(20), grayOf(20)};
    QTest::newRow("one and a half times as wide")
        << QSize(2, 1) << Pixels{grayOf(10), grayOf(20)} << QSize(3, 1) << Pixels{grayOf(10), grayOf(20), grayOf(20)};
    QTest::newRow("narrower samples the middle and the end")
        << QSize(3, 1) << Pixels{grayOf(10), grayOf(20), grayOf(30)} << QSize(2, 1) << Pixels{grayOf(10), grayOf(30)};
    QTest::newRow("twice as high") << QSize(1, 2) << Pixels{grayOf(10), grayOf(20)} << QSize(1, 4)
                                   << Pixels{grayOf(10), grayOf(10), grayOf(20), grayOf(20)};
    QTest::newRow("blocks on both axes") << QSize(2, 2) << Pixels{grayOf(1), grayOf(2), grayOf(3), grayOf(4)}
                                         << QSize(4, 4)
                                         << Pixels{grayOf(1), grayOf(1), grayOf(2), grayOf(2), grayOf(1), grayOf(1),
                                                   grayOf(2), grayOf(2), grayOf(3), grayOf(3), grayOf(4), grayOf(4),
                                                   grayOf(3), grayOf(3), grayOf(4), grayOf(4)};
    QTest::newRow("one pixel fills the image")
        << QSize(1, 1) << Pixels{pixelOf(128, 64, 32, 16)} << QSize(3, 2) << Pixels(6, pixelOf(128, 64, 32, 16));
}

void ResampleTest::enlargeNearestSamplesAtPixelCentres() {
    QFETCH(QSize, sourceSize);
    QFETCH(Pixels, source);
    QFETCH(QSize, size);
    QFETCH(Pixels, expected);

    const QImage result = enlargeNearest(imageOf(sourceSize, source), size, 1.0);
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), size);
    QCOMPARE(pixelsOf(result), expected);
}

void ResampleTest::enlargeNearestSetsTheRequestedScale() {
    const QImage source = randomImage(QSize(3, 3));
    const QImage before = source.copy();

    const QImage result = enlargeNearest(source, QSize(6, 6), 2.0);
    QVERIFY(!result.isNull());
    QCOMPARE(result.devicePixelRatio(), 2.0);
    QCOMPARE(result.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(result.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(source, before);
}

void ResampleTest::rejectsUnusableInput_data() {
    QTest::addColumn<QImage>("source");
    QTest::addColumn<QSize>("size");

    const QImage canonical = randomImage(QSize(4, 4));
    QImage opaque(QSize(4, 4), QImage::Format_RGB32);
    opaque.fill(Qt::black);

    QTest::newRow("null image") << QImage() << QSize(2, 2);
    QTest::newRow("not the canonical format") << opaque << QSize(2, 2);
    QTest::newRow("empty size") << canonical << QSize(0, 0);
    QTest::newRow("no width") << canonical << QSize(0, 2);
    QTest::newRow("no height") << canonical << QSize(2, 0);
    QTest::newRow("negative width") << canonical << QSize(-2, 2);
    QTest::newRow("negative height") << canonical << QSize(2, -2);
}

void ResampleTest::rejectsUnusableInput() {
    QFETCH(QImage, source);
    QFETCH(QSize, size);

    QVERIFY(areaAverage(source, size).isNull());
    QVERIFY(enlargeNearest(source, size, 1.0).isNull());
}

void ResampleTest::enlargeNearestRejectsAnUnusableScale_data() {
    QTest::addColumn<double>("scale");

    QTest::newRow("zero") << 0.0;
    QTest::newRow("negative") << -2.0;
    QTest::newRow("not a number") << std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("infinite") << std::numeric_limits<double>::infinity();
}

void ResampleTest::enlargeNearestRejectsAnUnusableScale() {
    QFETCH(double, scale);

    QVERIFY(enlargeNearest(randomImage(QSize(2, 2)), QSize(4, 4), scale).isNull());
}

QTEST_GUILESS_MAIN(ResampleTest)
#include "tst_Resample.moc"
