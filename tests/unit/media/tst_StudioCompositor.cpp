// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "media/studio/FrameScene.h"
#include "media/studio/StudioCompositor.h"

#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/rhi/qrhi.h>
#include <QtTest/QTest>

#include <memory>

using ariadshot::media::FrameScene;
using ariadshot::media::FrameSceneBuilder;
using ariadshot::media::StudioCompositor;

class StudioCompositorTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void frameSceneConstructionAndImmutability();
    void frameSceneRejectsOverlaySizeMismatch();
    void workingFormatIsRgba16FAndFinalIsRgba8();
    void renderRejectsInvalidSceneOrNullTargetsOnNullBackend();
    void renderReportsFailureOnInvalidInputs();
    void factoryRejectsNullRhi();
};

namespace {

QImage createSampleFrame(const QSize& size, QColor baseColor) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(baseColor);
    QPainter p(&image);
    p.setPen(Qt::white);
    p.drawLine(0, 0, size.width(), size.height());
    return image;
}

QImage createSampleOverlay(const QSize& size) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.fillRect(size.width() / 4, size.height() / 4, size.width() / 2, size.height() / 2, QColor(255, 0, 0, 128));
    return image;
}

} // namespace

void StudioCompositorTest::frameSceneConstructionAndImmutability() {
    // macshot/Capture/VideoSceneBuilder.swift@b4d4f3a
    const QSize size(1920, 1080);
    const QImage src = createSampleFrame(size, Qt::blue);
    const QImage overlay = createSampleOverlay(size);
    constexpr qint64 kTimestampNs = 1'000'000'000;

    const FrameScene scene = FrameSceneBuilder::build(src, kTimestampNs, overlay);

    QVERIFY(scene.isValid());
    QCOMPARE(scene.canvasSize(), size);
    QCOMPARE(scene.compositionTime(), kTimestampNs);
    QVERIFY(scene.hasOverlay());
    QCOMPARE(scene.sourceFrame().size(), size);
    QCOMPARE(scene.overlayLayer().size(), size);

    // Empty scene is invalid
    const FrameScene emptyScene;
    QVERIFY(!emptyScene.isValid());
    QCOMPARE(emptyScene.canvasSize(), QSize());
    QVERIFY(!emptyScene.hasOverlay());
}

void StudioCompositorTest::frameSceneRejectsOverlaySizeMismatch() {
    const QSize size(1920, 1080);
    const QImage src = createSampleFrame(size, Qt::blue);
    const QImage badOverlay = createSampleOverlay(QSize(1280, 720));

    const FrameScene scene = FrameSceneBuilder::build(src, 1'000'000'000, badOverlay);
    QVERIFY(!scene.isValid());
    QVERIFY(!scene.hasOverlay());
}

void StudioCompositorTest::workingFormatIsRgba16FAndFinalIsRgba8() {
    // arch §3.4.3 item 3: blending in RGBA16F linear, final pass to 8-bit sRGB
    // macshot/Capture/EffectsVideoCompositor.swift:160@b4d4f3a
    QRhiNullInitParams nullParams;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &nullParams));
    QVERIFY(rhi != nullptr);

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    QCOMPARE(compositor->workingFormat(), QRhiTexture::RGBA16F);
    QCOMPARE(compositor->finalFormat(), QRhiTexture::RGBA8);
}

void StudioCompositorTest::renderRejectsInvalidSceneOrNullTargetsOnNullBackend() {
    QRhiNullInitParams nullParams;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &nullParams));
    QVERIFY(rhi != nullptr);

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    const FrameScene invalidScene;
    // Calling render with invalid scene or null targets must report failure and not crash
    QVERIFY(!compositor->render(invalidScene, nullptr, nullptr));

    const QSize size(640, 480);
    const FrameScene validScene = FrameSceneBuilder::build(createSampleFrame(size, Qt::red), 0);
    QVERIFY(!compositor->render(validScene, nullptr, nullptr));

    std::unique_ptr<QRhiTexture> tex(rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    QVERIFY(tex->create());
    QRhiColorAttachment att(tex.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget({att}));
    QRhiCommandBuffer* cb = nullptr;
    // Target without render pass descriptor must report failure and not crash
    QVERIFY(!compositor->render(validScene, rt.get(), cb));
}

void StudioCompositorTest::renderReportsFailureOnInvalidInputs() {
    QRhiNullInitParams nullParams;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &nullParams));
    QVERIFY(rhi != nullptr);

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    const QSize size(640, 480);
    const QImage src = createSampleFrame(size, Qt::green);
    const QImage badOverlay = createSampleOverlay(QSize(320, 240));

    // Scene with mismatched overlay must fail render
    const FrameScene mismatchedScene = FrameSceneBuilder::build(src, 0, badOverlay);
    QVERIFY(!compositor->render(mismatchedScene, nullptr, nullptr));
}

void StudioCompositorTest::factoryRejectsNullRhi() {
    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(nullptr);
    QVERIFY(compositor == nullptr);
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    StudioCompositorTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_StudioCompositor.moc"
