// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/pixel/PixelSpan.h"

#include <QTest>

#include <array>
#include <climits>
#include <cstring>
#include <limits>
#include <utility>

using ariadshot::render::ConstPixelSpan;
using ariadshot::render::makeCanonicalImage;
using ariadshot::render::PixelSpan;

namespace {

constexpr qint64 kSmallestCoordinate = std::numeric_limits<qint64>::min();
constexpr qint64 kLargestCoordinate = std::numeric_limits<qint64>::max();

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
    void pixelsAreReachedByColumnAndRow();
    void rejectsColumnsOutsideTheRow_data();
    void rejectsColumnsOutsideTheRow();
    void rejectsRowsOutsideTheImageByCoordinates_data();
    void rejectsRowsOutsideTheImageByCoordinates();
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
    QTest::addColumn<qint64>("y");

    QTest::newRow("one above the first row") << qint64{-1};
    QTest::newRow("one past the last row") << qint64{3};
    QTest::newRow("far below") << qint64{1000};
    QTest::newRow("smallest int") << qint64{INT_MIN};
    QTest::newRow("largest int") << qint64{INT_MAX};
    // These two wrap to row 1 and row 0 when truncated to 32 bits, which is a valid row.
    QTest::newRow("a 64-bit row that wraps to row 1") << (qint64{1} << 32) + 1;
    QTest::newRow("a 64-bit row that wraps to row 0") << (qint64{1} << 32);
    QTest::newRow("smallest 64-bit row") << kSmallestCoordinate;
    QTest::newRow("largest 64-bit row") << kLargestCoordinate;
}

void PixelSpanTest::rejectsRowsOutsideTheImage() {
    QFETCH(qint64, y);

    QImage image = makeCanonicalImage(QSize(5, 3), 1.0);
    const PixelSpan writable(image);
    const ConstPixelSpan readOnly(std::as_const(image));
    QVERIFY(writable.row(y).empty());
    QVERIFY(readOnly.row(y).empty());
}

void PixelSpanTest::pixelsAreReachedByColumnAndRow() {
    QImage image = makeCanonicalImage(QSize(5, 3), 1.0);
    // The last pixel of one row and the first of the next sit next to each other in a flat index; a column and a row
    // must not mix them up.
    setRawPixel(image, 4, 0, 0xff00000aU);
    setRawPixel(image, 0, 1, 0xff00000bU);
    setRawPixel(image, 4, 2, 0xff00000cU);

    const PixelSpan writable(image);
    const ConstPixelSpan readOnly(std::as_const(image));
    QVERIFY(writable.pixel(4, 0) != nullptr);
    QVERIFY(readOnly.pixel(4, 0) != nullptr);
    QCOMPARE(*writable.pixel(4, 0), 0xff00000aU);
    QCOMPARE(*readOnly.pixel(0, 1), 0xff00000bU);
    QCOMPARE(*readOnly.pixel(4, 2), 0xff00000cU);
    QCOMPARE(*readOnly.pixel(0, 0), 0U);

    *writable.pixel(0, 2) = 0xff0d0e0fU;
    QCOMPARE(rawPixel(image, 0, 2), 0xff0d0e0fU);
    QCOMPARE(rawPixel(image, 4, 1), 0U);
    QCOMPARE(rawPixel(image, 4, 2), 0xff00000cU);
}

void PixelSpanTest::rejectsColumnsOutsideTheRow_data() {
    QTest::addColumn<qint64>("x");
    QTest::addColumn<qint64>("y");

    // The image is 5 pixels wide and 3 high. One past the last column of row 0 is the first pixel of row 1 in a flat
    // index, and one past the last column of the last row is outside the image's memory.
    const std::array<std::pair<const char*, qint64>, 3> rows = {{{"first row", 0}, {"middle row", 1}, {"last row", 2}}};
    const std::array<std::pair<const char*, qint64>, 9> columns = {{{"one left of the row", -1},
                                                                    {"one past the row", 5},
                                                                    {"two past the row", 6},
                                                                    {"a row's worth past", 10},
                                                                    {"far right", 1000},
                                                                    {"smallest int", INT_MIN},
                                                                    {"largest int", INT_MAX},
                                                                    {"smallest 64-bit", kSmallestCoordinate},
                                                                    {"largest 64-bit", kLargestCoordinate}}};
    for (const auto& [rowName, y] : rows) {
        for (const auto& [columnName, x] : columns) {
            QTest::newRow(qPrintable(QStringLiteral("%1, %2").arg(QLatin1String(rowName), QLatin1String(columnName))))
                << x << y;
        }
    }
    // A column that wraps to a valid column when truncated to 32 bits.
    QTest::newRow("a 64-bit column that wraps to column 1") << (qint64{1} << 32) + 1 << qint64{0};
}

void PixelSpanTest::rejectsColumnsOutsideTheRow() {
    QFETCH(qint64, x);
    QFETCH(qint64, y);

    QImage image = makeCanonicalImage(QSize(5, 3), 1.0);
    const PixelSpan writable(image);
    const ConstPixelSpan readOnly(std::as_const(image));
    QVERIFY(writable.pixel(x, y) == nullptr);
    QVERIFY(readOnly.pixel(x, y) == nullptr);
}

void PixelSpanTest::rejectsRowsOutsideTheImageByCoordinates_data() {
    QTest::addColumn<qint64>("x");
    QTest::addColumn<qint64>("y");

    QTest::newRow("one above the first row") << qint64{0} << qint64{-1};
    QTest::newRow("one past the last row") << qint64{4} << qint64{3};
    QTest::newRow("far below") << qint64{2} << qint64{1000};
    QTest::newRow("smallest int") << qint64{2} << qint64{INT_MIN};
    QTest::newRow("largest int") << qint64{2} << qint64{INT_MAX};
    QTest::newRow("a 64-bit row that wraps to row 1") << qint64{2} << (qint64{1} << 32) + 1;
    QTest::newRow("both outside") << qint64{-1} << qint64{3};
}

void PixelSpanTest::rejectsRowsOutsideTheImageByCoordinates() {
    QFETCH(qint64, x);
    QFETCH(qint64, y);

    QImage image = makeCanonicalImage(QSize(5, 3), 1.0);
    const PixelSpan writable(image);
    const ConstPixelSpan readOnly(std::as_const(image));
    QVERIFY(writable.pixel(x, y) == nullptr);
    QVERIFY(readOnly.pixel(x, y) == nullptr);
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
