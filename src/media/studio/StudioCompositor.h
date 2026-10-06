// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "media/studio/FrameScene.h"

#include <memory>
#include <rhi/qrhi.h>

namespace ariadshot::media {

// Studio compositor rendering FrameScene snapshots through QRhi.
// Shared by preview (QRhiWidget) and export (offscreen QRhi).
// macshot/Capture/VideoSceneRenderer.swift:150-235@b4d4f3a
// macshot/Capture/EffectsVideoCompositor.swift:160@b4d4f3a
// docs/spec/02-modules-and-interfaces.md §11
// docs/spec/03-rendering-contracts.md §3
// Thread: caller thread (GUI for preview, worker/export for offscreen)
class StudioCompositor {
  public:
    virtual ~StudioCompositor() = default;

    // Renders the scene into the specified render target using the given command buffer.
    // Returns true on success, false on invalid input or GPU resource creation failure.
    [[nodiscard]] virtual bool render(const FrameScene& scene, QRhiRenderTarget* target, QRhiCommandBuffer* cb) = 0;

    // Releases any cached RHI resources (textures, pipelines, buffers).
    virtual void releaseResources() = 0;

    // Working format: RGBA16F linear (arch §3.4.3 item 3, spec 03 §3.3)
    [[nodiscard]] virtual QRhiTexture::Format workingFormat() const noexcept = 0;

    // Final output format: 8-bit sRGB (RGBA8)
    [[nodiscard]] virtual QRhiTexture::Format finalFormat() const noexcept = 0;

    // Factory creating a concrete compositor instance bound to the given QRhi.
    [[nodiscard]] static std::unique_ptr<StudioCompositor> create(QRhi* rhi);
};

} // namespace ariadshot::media
