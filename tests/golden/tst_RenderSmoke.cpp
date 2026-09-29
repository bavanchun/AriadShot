// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "support/ImageCompare.h"

#include <QPainter>
#include <QTest>

using namespace Qt::StringLiterals;
using ariadshot::tests::ImageCompare;

// A regression golden for the canonical image factory and the raster painter, not a MacShot parity image. The scene
// uses opaque fills, one fill stored without blending, and one-pixel lines, all without antialiasing, so its pixels
// do not depend on the CPU's SIMD paths.
class RenderSmokeTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void sceneMatchesRegressionGolden();
};

namespace {

QImage renderScene() {
    QImage image = ariadshot::render::makeCanonicalImage(QSize(96, 64), 1.0);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(QRect(4, 4, 40, 24), QColor(0xe5, 0x39, 0x35));
    painter.fillRect(QRect(52, 4, 40, 24), QColor(0x1e, 0x88, 0xe5));
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(4, 36, 40, 24), QColor(0x43, 0xa0, 0x47, 0x80));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPen pen(QColor(0x21, 0x21, 0x21));
    pen.setWidth(1);
    painter.setPen(pen);
    painter.drawLine(QPoint(52, 36), QPoint(91, 36));
    painter.drawLine(QPoint(52, 36), QPoint(52, 59));
    painter.drawLine(QPoint(52, 59), QPoint(91, 36));
    painter.end();
    return image;
}

} // namespace

void RenderSmokeTest::sceneMatchesRegressionGolden() {
    const QString suite = u"render-smoke"_s;
    const QString name = u"scene"_s;
    const QImage actual = renderScene();
    QVERIFY(!actual.isNull());

    const QString outputDir = ImageCompare::outputDirectory();
    ImageCompare::writeCandidateIfRequested(outputDir, suite, name, actual);

    const QString path = ImageCompare::goldenPath(QStringLiteral(ARIADSHOT_GOLDEN_DIR), suite, name);
    const QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable(u"cannot read the golden image %1"_s.arg(path)));

    const auto result = ImageCompare::compareExact(actual, expected);
    if (!result.passed) {
        ImageCompare::writeFailureArtifacts(outputDir, name, actual, expected, result);
    }
    QVERIFY2(result.passed, qPrintable(result.failure));
}

QTEST_GUILESS_MAIN(RenderSmokeTest)
#include "tst_RenderSmoke.moc"
