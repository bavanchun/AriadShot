// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "media/studio/StudioCompositor.h"

#include <QtCore/QFile>
#include <QtCore/QIODevice>
#include <QtCore/QtGlobal>
#include <QtGui/rhi/qshader.h>

#include <array>

static void initStudioShaders() { Q_INIT_RESOURCE(studio_shaders); }

namespace ariadshot::media {

namespace {

struct Vertex {
    float x;
    float y;
    float u;
    float v;
};

[[nodiscard]] QShader loadShader(const QString& resourcePath) {
    initStudioShaders();
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

class RhiStudioCompositor final : public StudioCompositor {
  public:
    explicit RhiStudioCompositor(QRhi* rhi) : m_rhi(rhi) {}

    ~RhiStudioCompositor() override { releaseResources(); }

    [[nodiscard]] QRhiTexture::Format workingFormat() const noexcept override { return QRhiTexture::RGBA16F; }

    [[nodiscard]] QRhiTexture::Format finalFormat() const noexcept override { return QRhiTexture::RGBA8; }

    void releaseResources() override {
        delete m_sourcePipeline;
        m_sourcePipeline = nullptr;

        delete m_overlayPipeline;
        m_overlayPipeline = nullptr;

        delete m_outputPipeline;
        m_outputPipeline = nullptr;

        delete m_sourceBindings;
        m_sourceBindings = nullptr;

        delete m_overlayBindings;
        m_overlayBindings = nullptr;

        delete m_outputBindings;
        m_outputBindings = nullptr;

        delete m_workingRenderPassDesc;
        m_workingRenderPassDesc = nullptr;

        delete m_workingRenderTarget;
        m_workingRenderTarget = nullptr;

        delete m_workingTexture;
        m_workingTexture = nullptr;

        delete m_sourceTexture;
        m_sourceTexture = nullptr;

        delete m_overlayTexture;
        m_overlayTexture = nullptr;

        delete m_vertexBuffer;
        m_vertexBuffer = nullptr;

        delete m_sampler;
        m_sampler = nullptr;

        m_allocatedSize = QSize();
        m_targetPassDesc.reset();
    }

    bool render(const FrameScene& scene, QRhiRenderTarget* target, QRhiCommandBuffer* cb) override {
        if (!m_rhi || !target || !cb || !scene.isValid() || !target->renderPassDescriptor()) {
            return false;
        }

        if (scene.hasOverlay() && scene.overlayLayer().size() != scene.canvasSize()) {
            return false;
        }

        QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
        if (!ensureResources(scene.canvasSize(), target, u)) {
            u->release();
            if (!m_vertexBufferUploaded) {
                delete m_vertexBuffer;
                m_vertexBuffer = nullptr;
            }
            return false;
        }

        QImage src = scene.sourceFrame();
        if (src.format() != QImage::Format_RGBA8888) {
            src = src.convertToFormat(QImage::Format_RGBA8888);
        }
        u->uploadTexture(m_sourceTexture, src);

        if (scene.hasOverlay()) {
            QImage overlay = scene.overlayLayer();
            if (overlay.format() != QImage::Format_RGBA8888) {
                overlay = overlay.convertToFormat(QImage::Format_RGBA8888);
            }
            u->uploadTexture(m_overlayTexture, overlay);
        }

        cb->resourceUpdate(u);
        m_vertexBufferUploaded = true;

        // Pass 1 (+ Pass 2): Render to RGBA16F linear working target
        cb->beginPass(m_workingRenderTarget, Qt::transparent, {1.0f, 0});
        cb->setGraphicsPipeline(m_sourcePipeline);
        cb->setViewport({0.0f, 0.0f, static_cast<float>(scene.canvasSize().width()),
                         static_cast<float>(scene.canvasSize().height())});
        cb->setShaderResources(m_sourceBindings);
        const QRhiCommandBuffer::VertexInput vInput(m_vertexBuffer, 0);
        cb->setVertexInput(0, 1, &vInput);
        cb->draw(6);

        if (scene.hasOverlay()) {
            cb->setGraphicsPipeline(m_overlayPipeline);
            cb->setShaderResources(m_overlayBindings);
            cb->setVertexInput(0, 1, &vInput);
            cb->draw(6);
        }
        cb->endPass();

        // Pass 3: Convert linear RGBA16F to 8-bit sRGB in final render target
        cb->beginPass(target, Qt::black, {1.0f, 0});
        cb->setGraphicsPipeline(m_outputPipeline);
        cb->setViewport({0.0f, 0.0f, static_cast<float>(target->pixelSize().width()),
                         static_cast<float>(target->pixelSize().height())});
        cb->setShaderResources(m_outputBindings);
        const QRhiCommandBuffer::VertexInput vInputFinal(m_vertexBuffer, 6 * sizeof(Vertex));
        cb->setVertexInput(0, 1, &vInputFinal);
        cb->draw(6);
        cb->endPass();

        return true;
    }

  private:
    bool ensureResources(const QSize& size, QRhiRenderTarget* target, QRhiResourceUpdateBatch* u) {
        if (!m_rhi || !target || !target->renderPassDescriptor()) {
            return false;
        }

        if (!m_vertexBuffer || !m_vertexBufferUploaded) {
            delete m_vertexBuffer;
            m_vertexBuffer = nullptr;
            m_vertexBufferUploaded = false;

            const float topY = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
            const float bottomY = m_rhi->isYUpInNDC() ? -1.0f : 1.0f;

            const std::array<Vertex, 6> quadSource = {{
                {.x = -1.0f, .y = topY, .u = 0.0f, .v = 0.0f},
                {.x = 1.0f, .y = topY, .u = 1.0f, .v = 0.0f},
                {.x = -1.0f, .y = bottomY, .u = 0.0f, .v = 1.0f},
                {.x = -1.0f, .y = bottomY, .u = 0.0f, .v = 1.0f},
                {.x = 1.0f, .y = topY, .u = 1.0f, .v = 0.0f},
                {.x = 1.0f, .y = bottomY, .u = 1.0f, .v = 1.0f},
            }};

            const float topV = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
            const float bottomV = m_rhi->isYUpInFramebuffer() ? 0.0f : 1.0f;

            const std::array<Vertex, 6> quadWorking = {{
                {.x = -1.0f, .y = topY, .u = 0.0f, .v = topV},
                {.x = 1.0f, .y = topY, .u = 1.0f, .v = topV},
                {.x = -1.0f, .y = bottomY, .u = 0.0f, .v = bottomV},
                {.x = -1.0f, .y = bottomY, .u = 0.0f, .v = bottomV},
                {.x = 1.0f, .y = topY, .u = 1.0f, .v = topV},
                {.x = 1.0f, .y = bottomY, .u = 1.0f, .v = bottomV},
            }};

            m_vertexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 12 * sizeof(Vertex));
            if (!m_vertexBuffer->create()) {
                return false;
            }

            u->uploadStaticBuffer(m_vertexBuffer, 0, 6 * sizeof(Vertex), quadSource.data());
            u->uploadStaticBuffer(m_vertexBuffer, 6 * sizeof(Vertex), 6 * sizeof(Vertex), quadWorking.data());
        }

        if (!m_sampler) {
            m_sampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
            if (!m_sampler->create()) {
                return false;
            }
        }

        if (m_allocatedSize != size) {
            m_allocatedSize = QSize();

            delete m_sourcePipeline;
            m_sourcePipeline = nullptr;
            delete m_overlayPipeline;
            m_overlayPipeline = nullptr;
            delete m_outputPipeline;
            m_outputPipeline = nullptr;
            delete m_sourceBindings;
            m_sourceBindings = nullptr;
            delete m_overlayBindings;
            m_overlayBindings = nullptr;
            delete m_outputBindings;
            m_outputBindings = nullptr;
            delete m_workingRenderPassDesc;
            m_workingRenderPassDesc = nullptr;
            delete m_workingRenderTarget;
            m_workingRenderTarget = nullptr;
            delete m_workingTexture;
            m_workingTexture = nullptr;
            delete m_sourceTexture;
            m_sourceTexture = nullptr;
            delete m_overlayTexture;
            m_overlayTexture = nullptr;
            m_targetPassDesc.reset();

            const int maxTextureSize = m_rhi->resourceLimit(QRhi::TextureSizeMax);
            if (maxTextureSize > 0 && (size.width() > maxTextureSize || size.height() > maxTextureSize)) {
                return false;
            }

            m_sourceTexture = m_rhi->newTexture(QRhiTexture::RGBA8, size, 1, {});
            if (!m_sourceTexture->create()) {
                return false;
            }

            m_overlayTexture = m_rhi->newTexture(QRhiTexture::RGBA8, size, 1, {});
            if (!m_overlayTexture->create()) {
                return false;
            }

            m_workingTexture = m_rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget);
            if (!m_workingTexture->create()) {
                return false;
            }

            QRhiColorAttachment colorAtt(m_workingTexture);
            m_workingRenderTarget = m_rhi->newTextureRenderTarget({colorAtt});
            m_workingRenderPassDesc = m_workingRenderTarget->newCompatibleRenderPassDescriptor();
            m_workingRenderTarget->setRenderPassDescriptor(m_workingRenderPassDesc);
            if (!m_workingRenderTarget->create()) {
                return false;
            }

            m_sourceBindings = m_rhi->newShaderResourceBindings();
            m_sourceBindings->setBindings({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_sourceTexture,
                                                          m_sampler),
            });
            if (!m_sourceBindings->create()) {
                return false;
            }

            m_overlayBindings = m_rhi->newShaderResourceBindings();
            m_overlayBindings->setBindings({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_overlayTexture,
                                                          m_sampler),
            });
            if (!m_overlayBindings->create()) {
                return false;
            }

            m_outputBindings = m_rhi->newShaderResourceBindings();
            m_outputBindings->setBindings({
                QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_workingTexture,
                                                          m_sampler),
            });
            if (!m_outputBindings->create()) {
                return false;
            }

            const QShader vertShader = loadShader(QStringLiteral(":/media/studio/shaders/fullscreen.vert.qsb"));
            const QShader srcFragShader = loadShader(QStringLiteral(":/media/studio/shaders/source_linear.frag.qsb"));
            const QShader overlayFragShader =
                loadShader(QStringLiteral(":/media/studio/shaders/overlay_blend.frag.qsb"));
            if (!vertShader.isValid() || !srcFragShader.isValid() || !overlayFragShader.isValid()) {
                return false;
            }

            QRhiVertexInputLayout inputLayout;
            inputLayout.setBindings({
                QRhiVertexInputBinding(sizeof(Vertex)),
            });
            inputLayout.setAttributes({
                QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, offsetof(Vertex, x)),
                QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, offsetof(Vertex, u)),
            });

            m_sourcePipeline = m_rhi->newGraphicsPipeline();
            m_sourcePipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertShader},
                {QRhiShaderStage::Fragment, srcFragShader},
            });
            m_sourcePipeline->setVertexInputLayout(inputLayout);
            m_sourcePipeline->setShaderResourceBindings(m_sourceBindings);
            m_sourcePipeline->setRenderPassDescriptor(m_workingRenderPassDesc);
            if (!m_sourcePipeline->create()) {
                return false;
            }

            m_overlayPipeline = m_rhi->newGraphicsPipeline();
            m_overlayPipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertShader},
                {QRhiShaderStage::Fragment, overlayFragShader},
            });
            m_overlayPipeline->setVertexInputLayout(inputLayout);
            m_overlayPipeline->setShaderResourceBindings(m_overlayBindings);
            m_overlayPipeline->setRenderPassDescriptor(m_workingRenderPassDesc);
            // Blending in RGBA16F linear space: straight-alpha Over blend (SrcAlpha, OneMinusSrcAlpha)
            // matching IEC 61966-2-1 linear blending and arch §3.4.3.
            QRhiGraphicsPipeline::TargetBlend blendState;
            blendState.enable = true;
            blendState.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blendState.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blendState.srcAlpha = QRhiGraphicsPipeline::One;
            blendState.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            m_overlayPipeline->setTargetBlends({blendState});
            if (!m_overlayPipeline->create()) {
                return false;
            }

            m_allocatedSize = size;
        }

        if (!m_outputPipeline || !m_targetPassDesc ||
            !target->renderPassDescriptor()->isCompatible(m_targetPassDesc.get())) {
            delete m_outputPipeline;
            m_outputPipeline = nullptr;
            m_targetPassDesc.reset(target->renderPassDescriptor()->newCompatibleRenderPassDescriptor());

            const QShader vertShader = loadShader(QStringLiteral(":/media/studio/shaders/fullscreen.vert.qsb"));
            const QShader outFragShader = loadShader(QStringLiteral(":/media/studio/shaders/linear_to_srgb.frag.qsb"));
            if (!vertShader.isValid() || !outFragShader.isValid()) {
                return false;
            }

            QRhiVertexInputLayout inputLayout;
            inputLayout.setBindings({
                QRhiVertexInputBinding(sizeof(Vertex)),
            });
            inputLayout.setAttributes({
                QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, offsetof(Vertex, x)),
                QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, offsetof(Vertex, u)),
            });

            m_outputPipeline = m_rhi->newGraphicsPipeline();
            m_outputPipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertShader},
                {QRhiShaderStage::Fragment, outFragShader},
            });
            m_outputPipeline->setVertexInputLayout(inputLayout);
            m_outputPipeline->setShaderResourceBindings(m_outputBindings);
            m_outputPipeline->setRenderPassDescriptor(m_targetPassDesc.get());
            if (!m_outputPipeline->create()) {
                return false;
            }
        }

        return true;
    }

    QRhi* m_rhi = nullptr;
    QRhiBuffer* m_vertexBuffer = nullptr;
    bool m_vertexBufferUploaded = false;
    QRhiSampler* m_sampler = nullptr;
    QSize m_allocatedSize;

    QRhiTexture* m_sourceTexture = nullptr;
    QRhiTexture* m_overlayTexture = nullptr;
    QRhiTexture* m_workingTexture = nullptr;
    QRhiTextureRenderTarget* m_workingRenderTarget = nullptr;
    QRhiRenderPassDescriptor* m_workingRenderPassDesc = nullptr;

    QRhiShaderResourceBindings* m_sourceBindings = nullptr;
    QRhiShaderResourceBindings* m_overlayBindings = nullptr;
    QRhiShaderResourceBindings* m_outputBindings = nullptr;

    QRhiGraphicsPipeline* m_sourcePipeline = nullptr;
    QRhiGraphicsPipeline* m_overlayPipeline = nullptr;
    QRhiGraphicsPipeline* m_outputPipeline = nullptr;
    std::unique_ptr<QRhiRenderPassDescriptor> m_targetPassDesc;
};

} // namespace

std::unique_ptr<StudioCompositor> StudioCompositor::create(QRhi* rhi) {
    if (!rhi) {
        return nullptr;
    }
    return std::make_unique<RhiStudioCompositor>(rhi);
}

} // namespace ariadshot::media
