// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "media/studio/FrameScene.h"
#include "media/studio/StudioCompositor.h"
#include "support/ImageCompare.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QThread>
#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QPainter>
#if defined(ARIADSHOT_HAVE_VULKAN)
#include <QtGui/qvulkaninstance.h>
#endif
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

namespace {

static QString sApiFilter;

class StudioPreviewWidget final : public QRhiWidget {
  public:
    explicit StudioPreviewWidget(FrameScene scene, QWidget* parent = nullptr)
        : QRhiWidget(parent), m_scene(std::move(scene)) {
        setAutoRenderTarget(true);
    }

    ~StudioPreviewWidget() override { releaseResources(); }

    [[nodiscard]] bool initializationSucceeded() const noexcept { return m_initialized; }

  protected:
    void initialize(QRhiCommandBuffer* cb) override {
        Q_UNUSED(cb);
        m_compositor = StudioCompositor::create(rhi());
        m_initialized = (m_compositor != nullptr);
    }

    void render(QRhiCommandBuffer* cb) override {
        if (m_compositor && renderTarget()) {
            const bool ok = m_compositor->render(m_scene, renderTarget(), cb);
            Q_UNUSED(ok);
        }
    }

    void releaseResources() override {
        if (m_compositor) {
            m_compositor->releaseResources();
            m_compositor.reset();
        }
        m_initialized = false;
    }

  private:
    FrameScene m_scene;
    std::unique_ptr<StudioCompositor> m_compositor;
    bool m_initialized = false;
};

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
// sRGB decode per IEC 61966-2-1, straight-alpha Over blend (SrcAlpha, OneMinusSrcAlpha) of the overlay in linear light,
// sRGB encode.
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

FrameScene createTestScene(const QSize& size) {
    QImage source(size, QImage::Format_RGBA8888);
    // Vertical gradient on R covering low values 0..10 (linear segment c <= 0.04045 of IEC 61966-2-1)
    // and horizontal gradients on G and B covering 0..255.
    for (int y = 0; y < size.height(); ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(source.scanLine(y));
        const int r = (y * 255) / std::max(size.height() - 1, 1);
        for (int x = 0; x < size.width(); ++x) {
            const int g = (x * 255) / std::max(size.width() - 1, 1);
            const int b = ((x + y) * 255) / std::max(size.width() + size.height() - 2, 1);
            scanline[x] = qRgba(r, g, b, 255);
        }
    }

    QImage overlay(size, QImage::Format_RGBA8888);
    overlay.fill(Qt::transparent);
    // Alpha ramp from 0 to 255 across width in middle band
    const int startY = size.height() / 4;
    const int endY = (size.height() * 3) / 4;
    for (int y = startY; y < endY; ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const int a = (x * 255) / std::max(size.width() - 1, 1);
            const int r = 255 - ((y - startY) * 255) / std::max(endY - startY - 1, 1);
            const int g = (x * 255) / std::max(size.width() - 1, 1);
            const int b = 64;
            scanline[x] = qRgba(r, g, b, a);
        }
    }

    return FrameSceneBuilder::build(source, 0, overlay);
}

struct RhiContext {
#if defined(ARIADSHOT_HAVE_VULKAN)
    QVulkanInstance vkInst;
#endif
    std::unique_ptr<QOffscreenSurface> fallbackSurface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<StudioCompositor> compositor;

    static std::unique_ptr<RhiContext> create(const QString& apiName, QString* error = nullptr) {
        auto ctx = std::make_unique<RhiContext>();
        if (apiName == QStringLiteral("opengl")) {
            QRhiGles2InitParams params;
            ctx->fallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
            params.fallbackSurface = ctx->fallbackSurface.get();
            ctx->rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
            if (!ctx->rhi) {
                if (error) {
                    *error = QStringLiteral("Failed to create OpenGL QRhi");
                }
                return nullptr;
            }
        }
#if defined(ARIADSHOT_HAVE_VULKAN)
        else if (apiName == QStringLiteral("vulkan")) {
            if (!ctx->vkInst.create()) {
                if (error) {
                    *error = QStringLiteral("Failed to create QVulkanInstance");
                }
                return nullptr;
            }
            QRhiVulkanInitParams params;
            params.inst = &ctx->vkInst;
            ctx->rhi.reset(QRhi::create(QRhi::Vulkan, &params));
            if (!ctx->rhi) {
                if (error) {
                    *error = QStringLiteral("Failed to create Vulkan QRhi");
                }
                return nullptr;
            }
        }
#endif
        else {
            if (error) {
                *error = QStringLiteral("Unsupported or unconfigured API: %1").arg(apiName);
            }
            return nullptr;
        }

        ctx->compositor = StudioCompositor::create(ctx->rhi.get());
        if (!ctx->compositor) {
            if (error) {
                *error = QStringLiteral("StudioCompositor::create returned null");
            }
            return nullptr;
        }
        return ctx;
    }

    ~RhiContext() {
        compositor.reset();
        rhi.reset();
        fallbackSurface.reset();
    }
};

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

    if (!compositor->render(scene, rt.get(), cb)) {
        return {};
    }

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
    QImage image(p, size.width(), size.height(), static_cast<qsizetype>(size.width()) * 4, QImage::Format_RGBA8888);
    if (rhi->isYUpInFramebuffer()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        image = image.flipped(Qt::Vertical);
#else
        image = image.mirrored(false, true);
#endif
    }
    return image.copy();
}

} // namespace

class StudioCompositorRealTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void apiIsAssertedNeverSilentlySwitched();
    void apiIsAssertedNeverSilentlySwitched_data();

    void offscreenRenderIsDeterministic();
    void offscreenRenderIsDeterministic_data();

    void offscreenRenderMatchesCpuReference();
    void offscreenRenderMatchesCpuReference_data();

    void previewGrabMatchesPreEncode();
    void previewGrabMatchesPreEncode_data();

    void renderTwoSizesInARow();
    void renderTwoSizesInARow_data();

    void initTestCase();
};

void StudioCompositorRealTest::initTestCase() {
#if !defined(ARIADSHOT_HAVE_VULKAN)
    if (sApiFilter == QStringLiteral("vulkan")) {
        QFAIL("Vulkan is not supported in this build");
    }
#endif
}

static void populateApiData() {
    QTest::addColumn<QString>("apiName");
    if (sApiFilter.isEmpty() || sApiFilter == QStringLiteral("opengl")) {
        QTest::newRow("OpenGL") << QStringLiteral("opengl");
    }
#if defined(ARIADSHOT_HAVE_VULKAN)
    if (sApiFilter.isEmpty() || sApiFilter == QStringLiteral("vulkan")) {
        QTest::newRow("Vulkan") << QStringLiteral("vulkan");
    }
#endif
}

void StudioCompositorRealTest::apiIsAssertedNeverSilentlySwitched_data() { populateApiData(); }

void StudioCompositorRealTest::apiIsAssertedNeverSilentlySwitched() {
    QFETCH(QString, apiName);
    QString error;
    auto ctx = RhiContext::create(apiName, &error);
    if (!ctx) {
        QFAIL(qPrintable(error));
    }

    if (apiName == QStringLiteral("opengl")) {
        QCOMPARE(QLatin1StringView(ctx->rhi->backendName()), QLatin1StringView("OpenGL"));
    }
#if defined(ARIADSHOT_HAVE_VULKAN)
    else if (apiName == QStringLiteral("vulkan")) {
        QCOMPARE(QLatin1StringView(ctx->rhi->backendName()), QLatin1StringView("Vulkan"));
    }
#endif
}

void StudioCompositorRealTest::offscreenRenderIsDeterministic_data() { populateApiData(); }

void StudioCompositorRealTest::offscreenRenderIsDeterministic() {
    QFETCH(QString, apiName);
    QString error;
    auto ctx = RhiContext::create(apiName, &error);
    if (!ctx) {
        QFAIL(qPrintable(error));
    }

    const QSize size(160, 120);
    const FrameScene scene = createTestScene(size);

    const QImage render1 = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene);
    QVERIFY(!render1.isNull());

    const QImage render2 = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene);
    QVERIFY(!render2.isNull());

    QCOMPARE(render1.sizeInBytes(), render2.sizeInBytes());
    QCOMPARE(std::memcmp(render1.constBits(), render2.constBits(), render1.sizeInBytes()), 0);
}

void StudioCompositorRealTest::offscreenRenderMatchesCpuReference_data() { populateApiData(); }

void StudioCompositorRealTest::offscreenRenderMatchesCpuReference() {
    QFETCH(QString, apiName);
    QString error;
    auto ctx = RhiContext::create(apiName, &error);
    if (!ctx) {
        QFAIL(qPrintable(error));
    }

    const QSize size(160, 120);
    const FrameScene scene = createTestScene(size);

    const QImage preEncodeImage = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene);
    QVERIFY(!preEncodeImage.isNull());

    const QImage cpuReference = computeCpuReference(scene);
    const ComparisonResult cpuResult =
        ImageCompare::comparePerceptual(preEncodeImage, cpuReference, ImageCompare::kPresentation);
    QVERIFY(cpuResult.passed);
    QVERIFY(cpuResult.passingFraction >= ImageCompare::kPresentation.minPassingFraction);
    QVERIFY(cpuResult.maxDeltaE <= ImageCompare::kPresentation.maxDeltaE);
}

void StudioCompositorRealTest::previewGrabMatchesPreEncode_data() { populateApiData(); }

void StudioCompositorRealTest::previewGrabMatchesPreEncode() {
    QFETCH(QString, apiName);
    QString error;
    auto ctx = RhiContext::create(apiName, &error);
    if (!ctx) {
        QFAIL(qPrintable(error));
    }

    const QSize size(160, 120);
    const FrameScene scene = createTestScene(size);

    // 1. Offscreen pre-encode render
    const QImage preEncodeImage = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene);
    QVERIFY(!preEncodeImage.isNull());

    // 2. Preview widget grab
    StudioPreviewWidget widget(scene);
    if (apiName == QStringLiteral("opengl")) {
        widget.setApi(QRhiWidget::Api::OpenGL);
    }
#if defined(ARIADSHOT_HAVE_VULKAN)
    else if (apiName == QStringLiteral("vulkan")) {
        widget.setApi(QRhiWidget::Api::Vulkan);
    }
#endif
    widget.setFixedColorBufferSize(size);
    widget.setColorBufferFormat(QRhiWidget::TextureFormat::RGBA8);
    widget.resize(size);
    widget.show();

    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    for (int retries = 0; retries < 100 && !widget.initializationSucceeded(); ++retries) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    widget.update();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

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

    widget.hide();
    widget.close();
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

void StudioCompositorRealTest::renderTwoSizesInARow_data() { populateApiData(); }

void StudioCompositorRealTest::renderTwoSizesInARow() {
    QFETCH(QString, apiName);
    QString error;
    auto ctx = RhiContext::create(apiName, &error);
    if (!ctx) {
        QFAIL(qPrintable(error));
    }

    const QSize size1(160, 120);
    const FrameScene scene1 = createTestScene(size1);
    const QImage img1 = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene1);
    QVERIFY(!img1.isNull());
    QCOMPARE(img1.size(), size1);

    const QSize size2(320, 240);
    const FrameScene scene2 = createTestScene(size2);
    const QImage img2 = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene2);
    QVERIFY(!img2.isNull());
    QCOMPARE(img2.size(), size2);

    // Re-render size 1: verifies bindings and pipeline recreation behaves deterministically
    const QImage img3 = renderOffscreenToImage(ctx->rhi.get(), ctx->compositor.get(), scene1);
    QVERIFY(!img3.isNull());
    QCOMPARE(img3.size(), size1);
    QCOMPARE(img1.sizeInBytes(), img3.sizeInBytes());
    QCOMPARE(std::memcmp(img1.constBits(), img3.constBits(), img1.sizeInBytes()), 0);
}

int main(int argc, char** argv) {
    QList<char*> filteredArgv;
    for (int i = 0; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QStringLiteral("--api") && i + 1 < argc) {
            sApiFilter = QString::fromUtf8(argv[++i]).toLower();
        } else if (arg.startsWith(QStringLiteral("--api="))) {
            sApiFilter = arg.mid(6).toLower();
        } else {
            filteredArgv.append(argv[i]);
        }
    }

    int filteredArgc = filteredArgv.size();
    QApplication app(filteredArgc, filteredArgv.data());
    StudioCompositorRealTest tc;
    return QTest::qExec(&tc, filteredArgc, filteredArgv.data());
}

#include "tst_StudioCompositorReal.moc"
