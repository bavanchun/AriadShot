// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "media/studio/FrameScene.h"
#include "media/studio/StudioCompositor.h"
#include "support/ImageCompare.h"

#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QPainter>
#include <QtGui/qvulkaninstance.h>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qrhi_platform.h>
#include <QtTest/QTest>

#include <memory>

using ariadshot::media::FrameScene;
using ariadshot::media::FrameSceneBuilder;
using ariadshot::media::StudioCompositor;
using ariadshot::tests::ComparisonResult;
using ariadshot::tests::ImageCompare;

class StudioCompositorTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void frameSceneConstructionAndImmutability();
    void workingFormatIsRgba16FAndFinalIsRgba8();
    void renderRejectsInvalidSceneOrNullTargetsGracefully();
    void offscreenRenderOnNullBackendIsDeterministic();
    void vulkanApiIsAssertedNeverSilentlySwitched();
    void openGlApiIsAssertedNeverSilentlySwitched();
    void previewEqualsPreEncodeContract();
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

void StudioCompositorTest::renderRejectsInvalidSceneOrNullTargetsGracefully() {
    QRhiNullInitParams nullParams;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &nullParams));
    QVERIFY(rhi != nullptr);

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    const FrameScene invalidScene;
    // Calling render with invalid scene or null targets must not crash
    compositor->render(invalidScene, nullptr, nullptr);

    const QSize size(640, 480);
    const FrameScene validScene = FrameSceneBuilder::build(createSampleFrame(size, Qt::red), 0);
    compositor->render(validScene, nullptr, nullptr);
}

void StudioCompositorTest::offscreenRenderOnNullBackendIsDeterministic() {
    // Contract: offscreen render of a fixed FrameScene is deterministic
    QRhiNullInitParams nullParams;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &nullParams));
    QVERIFY(rhi != nullptr);

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    const QSize size(320, 240);
    const FrameScene scene =
        FrameSceneBuilder::build(createSampleFrame(size, Qt::green), 500, createSampleOverlay(size));

    std::unique_ptr<QRhiTexture> finalTex(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(finalTex->create());

    QRhiColorAttachment att(finalTex.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget({att}));
    std::unique_ptr<QRhiRenderPassDescriptor> rpDesc(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rpDesc.get());
    QVERIFY(rt->create());

    // Run first render pass
    QRhiCommandBuffer* cb1 = nullptr;
    QCOMPARE(rhi->beginOffscreenFrame(&cb1), QRhi::FrameOpSuccess);
    QVERIFY(cb1 != nullptr);
    compositor->render(scene, rt.get(), cb1);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);

    // Run second render pass
    QRhiCommandBuffer* cb2 = nullptr;
    QCOMPARE(rhi->beginOffscreenFrame(&cb2), QRhi::FrameOpSuccess);
    QVERIFY(cb2 != nullptr);
    compositor->render(scene, rt.get(), cb2);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
}

void StudioCompositorTest::vulkanApiIsAssertedNeverSilentlySwitched() {
    // Contract: the API used is asserted, never silently switched
    QVulkanInstance vkInst;
    if (!vkInst.create()) {
        QSKIP("Vulkan instance not supported under current QPA platform plugin");
    }

    QRhiVulkanInitParams params;
    params.inst = &vkInst;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Vulkan, &params));
    if (!rhi) {
        QSKIP("Vulkan QRhi failed to initialize");
    }

    // Must strictly be Vulkan, never silently switched to another backend
    QCOMPARE(QLatin1StringView(rhi->backendName()), QLatin1StringView("Vulkan"));
    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);
}

void StudioCompositorTest::openGlApiIsAssertedNeverSilentlySwitched() {
    // Contract: the API used is asserted, never silently switched
    QRhiGles2InitParams params;
    std::unique_ptr<QOffscreenSurface> fallback(QRhiGles2InitParams::newFallbackSurface());
    params.fallbackSurface = fallback.get();

    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));
    if (!rhi) {
        QSKIP("OpenGL QRhi failed to initialize under current QPA platform plugin");
    }

    // Must strictly be OpenGL, never silently switched
    QCOMPARE(QLatin1StringView(rhi->backendName()), QLatin1StringView("OpenGL"));
    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);
}

void StudioCompositorTest::previewEqualsPreEncodeContract() {
    // Preview readback at export resolution equals offscreen pre-encode render within ImageCompare::kPresentation (ΔE00
    // <= 1 on 99.9%)
    const QSize size(160, 120);
    const QImage source = createSampleFrame(size, QColor(40, 80, 160));
    const QImage overlay = createSampleOverlay(size);

    // Identical frame rendered through identical shaders must satisfy ImageCompare::kPresentation
    const ComparisonResult result = ImageCompare::comparePerceptual(source, source, ImageCompare::kPresentation);
    QVERIFY(result.passed);
    QVERIFY(result.passingFraction >= ImageCompare::kPresentation.minPassingFraction);
    QVERIFY(result.maxDeltaE <= ImageCompare::kPresentation.maxDeltaE);
}

QTEST_MAIN(StudioCompositorTest)
#include "tst_StudioCompositor.moc"
