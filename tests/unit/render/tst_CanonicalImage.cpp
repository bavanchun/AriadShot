// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"

#include <QColorSpace>
#include <QTest>

#include <algorithm>
#include <limits>

using ariadshot::render::makeCanonicalImage;

class CanonicalImageTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void hasCanonicalFormatColourSpaceAndScale_data();
    void hasCanonicalFormatColourSpaceAndScale();
    void rejectsInvalidInput_data();
    void rejectsInvalidInput();
};

void CanonicalImageTest::hasCanonicalFormatColourSpaceAndScale_data() {
    QTest::addColumn<QSize>("pixelSize");
    QTest::addColumn<qreal>("devicePixelRatio");

    QTest::newRow("one pixel") << QSize(1, 1) << 1.0;
    QTest::newRow("scale 1") << QSize(1920, 1200) << 1.0;
    QTest::newRow("scale 2") << QSize(2880, 1800) << 2.0;
    QTest::newRow("fractional scale") << QSize(1350, 2400) << 1.25;
}

void CanonicalImageTest::hasCanonicalFormatColourSpaceAndScale() {
    QFETCH(QSize, pixelSize);
    QFETCH(qreal, devicePixelRatio);

    const QImage image = makeCanonicalImage(pixelSize, devicePixelRatio);
    QVERIFY(!image.isNull());
    QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(image.size(), pixelSize);
    QCOMPARE(image.devicePixelRatio(), devicePixelRatio);
    QCOMPARE(image.deviceIndependentSize(), QSizeF(pixelSize) / devicePixelRatio);

    // Transparent premultiplied pixels are all-zero bytes.
    for (int y = 0; y < image.height(); ++y) {
        const uchar* row = image.constScanLine(y);
        QVERIFY2(std::all_of(row, row + qsizetype(image.width()) * 4, [](uchar byte) { return byte == 0; }),
                 qPrintable(QStringLiteral("row %1 is not transparent").arg(y)));
    }
}

void CanonicalImageTest::rejectsInvalidInput_data() {
    QTest::addColumn<QSize>("pixelSize");
    QTest::addColumn<qreal>("devicePixelRatio");

    QTest::newRow("zero width") << QSize(0, 10) << 1.0;
    QTest::newRow("zero height") << QSize(10, 0) << 1.0;
    QTest::newRow("negative width") << QSize(-1, 10) << 1.0;
    QTest::newRow("zero scale") << QSize(10, 10) << 0.0;
    QTest::newRow("negative scale") << QSize(10, 10) << -1.0;
    QTest::newRow("NaN scale") << QSize(10, 10) << std::numeric_limits<qreal>::quiet_NaN();
    QTest::newRow("infinite scale") << QSize(10, 10) << std::numeric_limits<qreal>::infinity();
}

void CanonicalImageTest::rejectsInvalidInput() {
    QFETCH(QSize, pixelSize);
    QFETCH(qreal, devicePixelRatio);

    QVERIFY(makeCanonicalImage(pixelSize, devicePixelRatio).isNull());
}

QTEST_GUILESS_MAIN(CanonicalImageTest)
#include "tst_CanonicalImage.moc"
