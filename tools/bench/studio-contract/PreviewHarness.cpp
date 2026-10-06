// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "media/studio/FrameScene.h"
#include "media/studio/StudioCompositor.h"
#include "support/ImageCompare.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QPainter>
#if QT_CONFIG(vulkan)
#include <QtGui/qvulkaninstance.h>
#endif
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qrhi_platform.h>
#include <QtWidgets/QApplication>
#include <QtWidgets/QRhiWidget>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

using ariadshot::media::FrameScene;
using ariadshot::media::FrameSceneBuilder;
using ariadshot::media::StudioCompositor;
using ariadshot::tests::ComparisonResult;
using ariadshot::tests::ImageCompare;

namespace {

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

FrameScene createBenchmarkScene(const QSize& size) {
    QImage source(size, QImage::Format_RGBA8888);
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

QImage renderOffscreenToImage(QRhi* rhi, StudioCompositor* compositor, const FrameScene& scene) {
    if (!rhi || !compositor || !scene.isValid()) {
        return {};
    }
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

int main(int argc, char** argv) {
    bool hasWaylandDisplayArg = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QStringLiteral("--wayland-display") && i + 1 < argc) {
            qputenv("WAYLAND_DISPLAY", argv[++i]);
            qputenv("QT_QPA_PLATFORM", "wayland");
            hasWaylandDisplayArg = true;
        } else if (arg.startsWith(QStringLiteral("--wayland-display="))) {
            const QString val = arg.mid(18);
            qputenv("WAYLAND_DISPLAY", val.toUtf8());
            qputenv("QT_QPA_PLATFORM", "wayland");
            hasWaylandDisplayArg = true;
        }
    }

    if (!hasWaylandDisplayArg) {
        const QString qpa = qEnvironmentVariable("QT_QPA_PLATFORM");
        if (qpa == QStringLiteral("offscreen")) {
            qunsetenv("WAYLAND_DISPLAY");
        } else {
            std::cerr << "Error: preview harness requires --wayland-display <socket> or QT_QPA_PLATFORM=offscreen. "
                      << "Live session displays and DRM/KMS platforms are forbidden.\n";
            return 1;
        }
    }

    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AriadShot Studio Preview Benchmark Harness"));
    parser.addHelpOption();

    const QCommandLineOption apiOption(QStringLiteral("api"), QStringLiteral("Graphics API to use: vulkan or opengl"),
                                       QStringLiteral("api"), QStringLiteral("vulkan"));
    parser.addOption(apiOption);

    const QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("Path to output JSON results file"),
                                       QStringLiteral("file"));
    parser.addOption(outOption);

    const QCommandLineOption waylandDisplayOption(QStringLiteral("wayland-display"),
                                                  QStringLiteral("Wayland socket to target"), QStringLiteral("socket"));
    parser.addOption(waylandDisplayOption);

    parser.process(app);

    const QString apiStr = parser.value(apiOption).toLower();
    const QString outFile = parser.value(outOption);

    const QSize exportResolution(1280, 720);
    const FrameScene scene = createBenchmarkScene(exportResolution);

    QJsonObject rootObj;
    rootObj[QStringLiteral("api")] = apiStr;
    rootObj[QStringLiteral("resolution")] =
        QStringLiteral("%1x%2").arg(exportResolution.width()).arg(exportResolution.height());

    QRhiWidget::Api rhiApi = QRhiWidget::Api::OpenGL;
#if QT_CONFIG(vulkan)
    QVulkanInstance vkInst;
#endif
    std::unique_ptr<QOffscreenSurface> glFallbackSurface;
    std::unique_ptr<QRhi> offscreenRhi;

    if (apiStr == QStringLiteral("opengl")) {
        rhiApi = QRhiWidget::Api::OpenGL;
        QRhiGles2InitParams glParams;
        glFallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
        glParams.fallbackSurface = glFallbackSurface.get();
        offscreenRhi.reset(QRhi::create(QRhi::OpenGLES2, &glParams));
    } else {
#if QT_CONFIG(vulkan)
        rhiApi = QRhiWidget::Api::Vulkan;
        if (vkInst.create()) {
            QRhiVulkanInitParams vkParams;
            vkParams.inst = &vkInst;
            offscreenRhi.reset(QRhi::create(QRhi::Vulkan, &vkParams));
        }
#else
        std::cerr << "Vulkan support not enabled in Qt\n";
#endif
    }

    // 1. Offscreen export path render
    QImage preEncodeImage;
    if (offscreenRhi) {
        std::unique_ptr<StudioCompositor> compositor = StudioCompositor::create(offscreenRhi.get());
        if (compositor) {
            preEncodeImage = renderOffscreenToImage(offscreenRhi.get(), compositor.get(), scene);
        }
    }

    // 2. Offscreen preview widget render
    StudioPreviewWidget widget(scene);
    widget.setApi(rhiApi);
    widget.setFixedColorBufferSize(exportResolution);
    widget.setColorBufferFormat(QRhiWidget::TextureFormat::RGBA8);
    widget.resize(exportResolution);
    widget.show();

    constexpr int kMaxRetries = 100;
    int retries = 0;
    while (!widget.initializationSucceeded() && retries < kMaxRetries) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
        ++retries;
    }
    widget.update();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QThread::msleep(50);

    QImage previewGrab = widget.grabFramebuffer();
    if (!previewGrab.isNull() && previewGrab.format() != QImage::Format_RGBA8888) {
        previewGrab = previewGrab.convertToFormat(QImage::Format_RGBA8888);
    }

    bool overallPassed = false;
    if (previewGrab.isNull() || preEncodeImage.isNull()) {
        rootObj[QStringLiteral("status")] = QStringLiteral("not_taken");
        QString failureReason;
        if (!offscreenRhi) {
            failureReason = QStringLiteral("Offscreen QRhi initialization failed on API '%1'").arg(apiStr);
        } else if (preEncodeImage.isNull()) {
            failureReason = QStringLiteral("Pre-encode offscreen render failed on API '%1'").arg(apiStr);
        } else {
            failureReason =
                QStringLiteral("QRhiWidget grabFramebuffer failed on API '%1' under current platform plugin")
                    .arg(apiStr);
        }
        rootObj[QStringLiteral("reason")] = failureReason;
        std::cout << "[preview-harness] Measurement not taken: " << failureReason.toStdString() << ".\n";
    } else {
        const QImage cpuReference = computeCpuReference(scene);
        const ComparisonResult cpuResult =
            ImageCompare::comparePerceptual(preEncodeImage, cpuReference, ImageCompare::kPresentation);
        const ComparisonResult previewResult =
            ImageCompare::comparePerceptual(previewGrab, preEncodeImage, ImageCompare::kPresentation);

        overallPassed = cpuResult.passed && previewResult.passed;

        rootObj[QStringLiteral("status")] = overallPassed ? QStringLiteral("passed") : QStringLiteral("failed");
        rootObj[QStringLiteral("passed")] = overallPassed;

        rootObj[QStringLiteral("pre_encode_vs_cpu_passed")] = cpuResult.passed;
        rootObj[QStringLiteral("pre_encode_vs_cpu_passing_fraction")] = cpuResult.passingFraction;
        rootObj[QStringLiteral("pre_encode_vs_cpu_max_delta_e")] = cpuResult.maxDeltaE;
        rootObj[QStringLiteral("pre_encode_vs_cpu_p99_delta_e")] = cpuResult.p99DeltaE;

        rootObj[QStringLiteral("preview_vs_pre_encode_passed")] = previewResult.passed;
        rootObj[QStringLiteral("preview_vs_pre_encode_passing_fraction")] = previewResult.passingFraction;
        rootObj[QStringLiteral("preview_vs_pre_encode_max_delta_e")] = previewResult.maxDeltaE;
        rootObj[QStringLiteral("preview_vs_pre_encode_p99_delta_e")] = previewResult.p99DeltaE;

        rootObj[QStringLiteral("passing_fraction")] =
            std::min(cpuResult.passingFraction, previewResult.passingFraction);
        rootObj[QStringLiteral("max_delta_e")] = std::max(cpuResult.maxDeltaE, previewResult.maxDeltaE);
        rootObj[QStringLiteral("p99_delta_e")] = std::max(cpuResult.p99DeltaE, previewResult.p99DeltaE);

        std::cout << "[preview-harness] API: " << apiStr.toStdString()
                  << " overall: " << (overallPassed ? "PASSED" : "FAILED") << "\n"
                  << "  Pre-encode vs CPU Ref: " << (cpuResult.passed ? "PASSED" : "FAILED")
                  << " passingFraction: " << cpuResult.passingFraction << " maxDeltaE: " << cpuResult.maxDeltaE
                  << " p99DeltaE: " << cpuResult.p99DeltaE << "\n"
                  << "  Preview vs Pre-encode: " << (previewResult.passed ? "PASSED" : "FAILED")
                  << " passingFraction: " << previewResult.passingFraction << " maxDeltaE: " << previewResult.maxDeltaE
                  << " p99DeltaE: " << previewResult.p99DeltaE << "\n";
    }

    if (!outFile.isEmpty()) {
        const QFileInfo fileInfo(outFile);
        QDir().mkpath(fileInfo.absolutePath());
        QFile file(outFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
            file.close();
            std::cout << "[preview-harness] Report written to: " << outFile.toStdString() << "\n";
        }
    }

    return overallPassed ? 0 : 1;
}
