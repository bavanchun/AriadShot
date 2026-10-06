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
#include <QtGui/qvulkaninstance.h>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qrhi_platform.h>
#include <QtWidgets/QApplication>
#include <QtWidgets/QRhiWidget>

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
            m_compositor->render(m_scene, renderTarget(), cb);
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

FrameScene createBenchmarkScene(const QSize& size) {
    QImage source(size, QImage::Format_RGBA8888);
    source.fill(Qt::black);
    {
        QPainter p(&source);
        for (int y = 0; y < size.height(); ++y) {
            const int r = (y * 255) / std::max(size.height() - 1, 1);
            p.setPen(QColor(r, 120, 200));
            p.drawLine(0, y, size.width(), y);
        }
    }

    QImage overlay(size, QImage::Format_RGBA8888);
    overlay.fill(Qt::transparent);
    {
        QPainter p(&overlay);
        p.setBrush(QColor(255, 255, 0, 128));
        p.setPen(Qt::NoPen);
        p.drawRect(size.width() / 4, size.height() / 4, size.width() / 2, size.height() / 2);
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
    QImage image(p, size.width(), size.height(), static_cast<qsizetype>(size.width()) * 4, QImage::Format_RGBA8888);
    if (rhi->isYUpInFramebuffer()) {
        image = image.flipped(Qt::Vertical);
    }
    return image.copy();
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QStringLiteral("--wayland-display") && i + 1 < argc) {
            qputenv("WAYLAND_DISPLAY", argv[++i]);
            qputenv("QT_QPA_PLATFORM", "wayland");
        } else if (arg.startsWith(QStringLiteral("--wayland-display="))) {
            const QString val = arg.mid(18);
            qputenv("WAYLAND_DISPLAY", val.toUtf8());
            qputenv("QT_QPA_PLATFORM", "wayland");
        }
    }

    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen") ||
         qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))) {
        qputenv("QT_QPA_PLATFORM", "eglfs");
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

    QRhiWidget::Api rhiApi = QRhiWidget::Api::Vulkan;
    QVulkanInstance vkInst;
    std::unique_ptr<QOffscreenSurface> glFallbackSurface;
    std::unique_ptr<QRhi> offscreenRhi;

    if (apiStr == QStringLiteral("opengl")) {
        rhiApi = QRhiWidget::Api::OpenGL;
        QRhiGles2InitParams glParams;
        glFallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
        glParams.fallbackSurface = glFallbackSurface.get();
        offscreenRhi.reset(QRhi::create(QRhi::OpenGLES2, &glParams));
    } else {
        rhiApi = QRhiWidget::Api::Vulkan;
        if (vkInst.create()) {
            QRhiVulkanInitParams vkParams;
            vkParams.inst = &vkInst;
            offscreenRhi.reset(QRhi::create(QRhi::Vulkan, &vkParams));
        }
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

    QImage previewGrab = widget.grabFramebuffer();
    if (!previewGrab.isNull() && previewGrab.format() != QImage::Format_RGBA8888) {
        previewGrab = previewGrab.convertToFormat(QImage::Format_RGBA8888);
    }

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
        const ComparisonResult compResult =
            ImageCompare::comparePerceptual(previewGrab, preEncodeImage, ImageCompare::kPresentation);

        rootObj[QStringLiteral("status")] = compResult.passed ? QStringLiteral("passed") : QStringLiteral("failed");
        rootObj[QStringLiteral("passing_fraction")] = compResult.passingFraction;
        rootObj[QStringLiteral("max_delta_e")] = compResult.maxDeltaE;
        rootObj[QStringLiteral("p99_delta_e")] = compResult.p99DeltaE;
        rootObj[QStringLiteral("passed")] = compResult.passed;

        std::cout << "[preview-harness] API: " << apiStr.toStdString()
                  << " result: " << (compResult.passed ? "PASSED" : "FAILED")
                  << " passingFraction: " << compResult.passingFraction << " maxDeltaE: " << compResult.maxDeltaE
                  << " p99DeltaE: " << compResult.p99DeltaE << "\n";
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

    return 0;
}
