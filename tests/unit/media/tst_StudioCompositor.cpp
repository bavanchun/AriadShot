// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libxkbcommon\n"
           "leak:QEglFSIntegration\n"
           "leak:libQt6EglFS\n";
}
#endif

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
#include <QtWidgets/QApplication>
#include <QtWidgets/QRhiWidget>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

using ariadshot::media::FrameScene;
using ariadshot::media::FrameSceneBuilder;
using ariadshot::media::StudioCompositor;
using ariadshot::tests::ComparisonResult;
using ariadshot::tests::ImageCompare;

class StudioPreviewWidget final : public QRhiWidget {
  public:
    explicit StudioPreviewWidget(FrameScene scene, QWidget* parent = nullptr)
        : QRhiWidget(parent), m_scene(std::move(scene)) {
        setAutoRenderTarget(true);
    }

    ~StudioPreviewWidget() override { releaseResources(); }

  protected:
    void initialize(QRhiCommandBuffer* cb) override {
        Q_UNUSED(cb);
        m_compositor = StudioCompositor::create(rhi());
    }

    void render(QRhiCommandBuffer* cb) override {
        if (m_compositor && renderTarget()) {
            m_compositor->render(m_scene, renderTarget(), cb);
        }
    }

    void releaseResources() override {
        if (m_compositor) {
            m_compositor->releaseResources();
            m_compositor.reset();
        }
    }

  private:
    FrameScene m_scene;
    std::unique_ptr<StudioCompositor> m_compositor;
};

class StudioCompositorTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void frameSceneConstructionAndImmutability();
    void workingFormatIsRgba16FAndFinalIsRgba8();
    void renderRejectsInvalidSceneOrNullTargetsOnNullBackend();
    void offscreenRenderOnOpenGlBackendIsDeterministic();
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

// IEC 61966-2-1 sRGB to linear conversion (macshot/Capture/EffectsVideoCompositor.swift:160@b4d4f3a)
inline float srgbToLinear(float c) {
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// IEC 61966-2-1 linear to sRGB conversion (docs/spec/03-rendering-contracts.md §3.3)
inline float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    if (c <= 0.0031308f) {
        return c * 12.92f;
    }
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Independent CPU reference implementation of the compositor math:
// sRGB decode per IEC 61966-2-1, premultiplied blend of the overlay in linear light, sRGB encode.
QImage computeCpuReference(const FrameScene& scene) {
    const QSize size = scene.canvasSize();
    QImage ref(size, QImage::Format_RGBA8888);
    const QImage src = scene.sourceFrame().convertToFormat(QImage::Format_RGBA8888);
    const bool hasOv = scene.hasOverlay();
    const QImage ov = hasOv ? scene.overlayLayer().convertToFormat(QImage::Format_RGBA8888) : QImage();

    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const QRgb sPixel = src.pixel(x, y);
            const float sR = srgbToLinear(static_cast<float>(qRed(sPixel)) / 255.0f);
            const float sG = srgbToLinear(static_cast<float>(qGreen(sPixel)) / 255.0f);
            const float sB = srgbToLinear(static_cast<float>(qBlue(sPixel)) / 255.0f);
            const float sA = static_cast<float>(qAlpha(sPixel)) / 255.0f;

            float outR = sR;
            float outG = sG;
            float outB = sB;
            float outA = sA;

            if (hasOv) {
                const QRgb oPixel = ov.pixel(x, y);
                const float oR = srgbToLinear(static_cast<float>(qRed(oPixel)) / 255.0f);
                const float oG = srgbToLinear(static_cast<float>(qGreen(oPixel)) / 255.0f);
                const float oB = srgbToLinear(static_cast<float>(qBlue(oPixel)) / 255.0f);
                const float oA = static_cast<float>(qAlpha(oPixel)) / 255.0f;

                outR = oR * oA + sR * (1.0f - oA);
                outG = oG * oA + sG * (1.0f - oA);
                outB = oB * oA + sB * (1.0f - oA);
                outA = oA + sA * (1.0f - oA);
            }

            const int fR = std::clamp(static_cast<int>(std::round(linearToSrgb(outR) * 255.0f)), 0, 255);
            const int fG = std::clamp(static_cast<int>(std::round(linearToSrgb(outG) * 255.0f)), 0, 255);
            const int fB = std::clamp(static_cast<int>(std::round(linearToSrgb(outB) * 255.0f)), 0, 255);
            const int fA = std::clamp(static_cast<int>(std::round(outA * 255.0f)), 0, 255);

            ref.setPixelColor(x, y, QColor(fR, fG, fB, fA));
        }
    }
    return ref;
}

QImage renderOffscreenToImage(QRhi* rhi, StudioCompositor* compositor, const FrameScene& scene) {
    const QSize size = scene.canvasSize();
    std::unique_ptr<QRhiTexture> finalTex(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!finalTex->create()) {
        return {};
    }

    QRhiColorAttachment att(finalTex.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget({att}));
    std::unique_ptr<QRhiRenderPassDescriptor> rpDesc(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rpDesc.get());
    if (!rt->create()) {
        return {};
    }

    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        return {};
    }

    compositor->render(scene, rt.get(), cb);

    QRhiReadbackResult rbResult;
    QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
    u->readBackTexture(QRhiReadbackDescription(finalTex.get()), &rbResult);
    cb->resourceUpdate(u);

    if (rhi->endOffscreenFrame() != QRhi::FrameOpSuccess) {
        return {};
    }
    rhi->finish();

    if (rbResult.data.isEmpty()) {
        return {};
    }

    const auto* p = reinterpret_cast<const uchar*>(rbResult.data.constData());
    QImage image(p, size.width(), size.height(), size.width() * 4, QImage::Format_RGBA8888);
    if (rhi->isYUpInFramebuffer()) {
        image = image.flipped(Qt::Vertical);
    }
    return image.copy();
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

void StudioCompositorTest::renderRejectsInvalidSceneOrNullTargetsOnNullBackend() {
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

void StudioCompositorTest::offscreenRenderOnOpenGlBackendIsDeterministic() {
    // Contract: offscreen render of a fixed FrameScene on real backend is deterministic (byte-identical)
    QRhiGles2InitParams params;
    std::unique_ptr<QOffscreenSurface> fallback(QRhiGles2InitParams::newFallbackSurface());
    params.fallbackSurface = fallback.get();

    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));
    if (!rhi) {
        QSKIP("OpenGL QRhi failed to initialize under current QPA platform plugin");
    }

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    const QSize size(320, 240);
    const FrameScene scene =
        FrameSceneBuilder::build(createSampleFrame(size, Qt::green), 500, createSampleOverlay(size));

    const QImage render1 = renderOffscreenToImage(rhi.get(), compositor.get(), scene);
    QVERIFY(!render1.isNull());

    const QImage render2 = renderOffscreenToImage(rhi.get(), compositor.get(), scene);
    QVERIFY(!render2.isNull());

    QCOMPARE(render1.sizeInBytes(), render2.sizeInBytes());
    QCOMPARE(std::memcmp(render1.constBits(), render2.constBits(), render1.sizeInBytes()), 0);
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
    QRhiGles2InitParams params;
    std::unique_ptr<QOffscreenSurface> fallback(QRhiGles2InitParams::newFallbackSurface());
    params.fallbackSurface = fallback.get();

    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));
    if (!rhi) {
        QSKIP("OpenGL QRhi failed to initialize under current QPA platform plugin");
    }

    std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(rhi.get());
    QVERIFY(compositor != nullptr);

    const QSize size(160, 120);
    const QImage source = createSampleFrame(size, QColor(40, 80, 160));
    const QImage overlay = createSampleOverlay(size);
    const FrameScene scene = FrameSceneBuilder::build(source, 0, overlay);

    // 1. Offscreen pre-encode render on real backend
    const QImage preEncodeImage = renderOffscreenToImage(rhi.get(), compositor.get(), scene);
    QVERIFY(!preEncodeImage.isNull());

    // (a) Compare offscreen pre-encode render against independently computed CPU reference (IEC 61966-2-1 math)
    const QImage cpuReference = computeCpuReference(scene);
    const ComparisonResult cpuResult =
        ImageCompare::comparePerceptual(preEncodeImage, cpuReference, ImageCompare::kPresentation);
    QVERIFY(cpuResult.passed);
    QVERIFY(cpuResult.passingFraction >= ImageCompare::kPresentation.minPassingFraction);
    QVERIFY(cpuResult.maxDeltaE <= ImageCompare::kPresentation.maxDeltaE);

    // (b) Compare preview grab (QRhiWidget grabFramebuffer) against pre-encode offscreen render
    StudioPreviewWidget widget(scene);
    widget.setApi(QRhiWidget::Api::OpenGL);
    widget.setFixedColorBufferSize(size);
    widget.setColorBufferFormat(QRhiWidget::TextureFormat::RGBA8);
    widget.resize(size);

    QImage previewGrab = widget.grabFramebuffer();
    QVERIFY(!previewGrab.isNull());
    if (previewGrab.format() != QImage::Format_RGBA8888) {
        previewGrab = previewGrab.convertToFormat(QImage::Format_RGBA8888);
    }

    const ComparisonResult previewResult =
        ImageCompare::comparePerceptual(previewGrab, preEncodeImage, ImageCompare::kPresentation);
    QVERIFY(previewResult.passed);
    QVERIFY(previewResult.passingFraction >= ImageCompare::kPresentation.minPassingFraction);
    QVERIFY(previewResult.maxDeltaE <= ImageCompare::kPresentation.maxDeltaE);
}

int main(int argc, char** argv) {
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen") ||
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "eglfs");
    }
    QApplication app(argc, argv);
    StudioCompositorTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_StudioCompositor.moc"
