// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/pixel/PixelSpan.h"

#include <QTest>

#include <climits>
#include <cstring>

using ariadshot::render::ConstPixelSpan;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::PixelSpan;

namespace {

// Raw access to the image bytes that does not go through PixelSpan, so the tests check the view against the memory.
quint32 rawPixel(const QImage& image, int x, int y) {
    quint32 value = 0;
    std::memcpy(&value, image.constScanLine(y) + static_cast<qsizetype>(x) * 4, sizeof value);
    return value;
}

void setRawPixel(QImage& image, int x, int y, quint32 value) {
    std::memcpy(image.scanLine(y) + static_cast<qsizetype>(x) * 4, &value, sizeof value);
}

} // namespace

class PixelSpanTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void rowsHoldExactlyOneScanLineOfThePixels();
    void writesLandInTheImage();
    void rejectsRowsOutsideTheImage_data();
    void rejectsRowsOutsideTheImage();
    void isEmptyForImagesThatAreNotCanonical_data();
    void isEmptyForImagesThatAreNotCanonical();
    void aWritableViewDetachesASharedImage();
    void aReadOnlyViewKeepsTheImageShared();
};

void PixelSpanTest::rowsHoldExactlyOneScanLineOfThePixels() {
    QImage image = makeCanonicalImage(QSize(5, 3), 1.0);
    setRawPixel(image, 4, 2, 0xff102030U);
    setRawPixel(image, 0, 1, 0x80402010U);

    const ConstPixelSpan view(std::as_const(image));
    QVERIFY(!view.isEmpty());
    QCOMPARE(view.width(), 5);
    QCOMPARE(view.height(), 3);
    for (int y = 0; y < view.height(); ++y) {
        QCOMPARE(view.row(y).size(), std::size_t{5});
    }
    QCOMPARE(view.row(2)[4], 0xff102030U);
    QCOMPARE(view.row(1)[0], 0x80402010U);
    QCOMPARE(view.row(0)[0], 0U);
}

void PixelSpanTest::writesLandInTheImage() {
    QImage image = makeCanonicalImage(QSize(4, 2), 1.0);
    const PixelSpan view(image);
    view.row(1)[3] = 0xff0a0b0cU;

    QCOMPARE(rawPixel(image, 3, 1), 0xff0a0b0cU);
    QCOMPARE(rawPixel(image, 2, 1), 0U);
    QCOMPARE(rawPixel(image, 3, 0), 0U);
}

void PixelSpanTest::rejectsRowsOutsideTheImage_data() {
    QTest::addColumn<int>("y");

    QTest::newRow("one above the first row") << -1;
    QTest::newRow("one past the last row") << 3;
    QTest::newRow("far below") << 1000;
    QTest::newRow("smallest int") << INT_MIN;
    QTest::newRow("largest int") << INT_MAX;
}

void PixelSpanTest::rejectsRowsOutsideTheImage() {
    QFETCH(int, y);

    QImage image = makeCanonicalImage(QSize(5, 3), 1.0);
    const PixelSpan writable(image);
    const ConstPixelSpan readOnly(std::as_const(image));
    QVERIFY(writable.row(y).empty());
    QVERIFY(readOnly.row(y).empty());
}

void PixelSpanTest::isEmptyForImagesThatAreNotCanonical_data() {
    QTest::addColumn<QImage::Format>("format");

    QTest::newRow("not premultiplied") << QImage::Format_ARGB32;
    QTest::newRow("no alpha channel") << QImage::Format_RGB32;
    QTest::newRow("eight bit gray") << QImage::Format_Grayscale8;
    QTest::newRow("sixteen bit components") << QImage::Format_RGBA64_Premultiplied;
}

void PixelSpanTest::isEmptyForImagesThatAreNotCanonical() {
    QFETCH(QImage::Format, format);

    QImage image(QSize(5, 3), format);
    image.fill(Qt::black);
    const PixelSpan writable(image);
    const ConstPixelSpan readOnly(std::as_const(image));
    QVERIFY(writable.isEmpty());
    QVERIFY(readOnly.isEmpty());
    QVERIFY(writable.row(0).empty());
    QVERIFY(readOnly.row(0).empty());

    QImage null;
    QVERIFY(PixelSpan(null).isEmpty());
    QVERIFY(ConstPixelSpan(std::as_const(null)).isEmpty());
}

void PixelSpanTest::aWritableViewDetachesASharedImage() {
    QImage image = makeCanonicalImage(QSize(2, 2), 1.0);
    const QImage sharedCopy = image;

    const PixelSpan view(image);
    view.row(0)[0] = 0xffffffffU;

    QCOMPARE(rawPixel(image, 0, 0), 0xffffffffU);
    QCOMPARE(rawPixel(sharedCopy, 0, 0), 0U);
}

void PixelSpanTest::aReadOnlyViewKeepsTheImageShared() {
    QImage image = makeCanonicalImage(QSize(2, 2), 1.0);
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): the implicitly shared copy is what the test observes
    const QImage sharedCopy = image;

    const ConstPixelSpan view(image);
    QVERIFY(!view.isEmpty());
    QCOMPARE(image.constBits(), sharedCopy.constBits());
}

QTEST_GUILESS_MAIN(PixelSpanTest)
#include "tst_PixelSpan.moc"
