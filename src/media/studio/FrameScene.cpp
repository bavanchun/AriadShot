// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "media/studio/FrameScene.h"

#include <utility>

namespace ariadshot::media {

FrameScene::FrameScene(QImage sourceFrame, qint64 compositionTimeNs, QImage overlayLayer)
    : m_sourceFrame(std::move(sourceFrame)), m_compositionTimeNs(compositionTimeNs),
      m_overlayLayer(std::move(overlayLayer)) {
    if (!m_sourceFrame.isNull() && m_sourceFrame.width() > 0 && m_sourceFrame.height() > 0) {
        m_canvasSize = m_sourceFrame.size();
        m_valid = true;
        if (!m_overlayLayer.isNull() && m_overlayLayer.size() != m_canvasSize) {
            m_valid = false;
        }
    }
}

bool FrameScene::isValid() const noexcept { return m_valid; }

const QImage& FrameScene::sourceFrame() const noexcept { return m_sourceFrame; }

qint64 FrameScene::compositionTime() const noexcept { return m_compositionTimeNs; }

const QImage& FrameScene::overlayLayer() const noexcept { return m_overlayLayer; }

bool FrameScene::hasOverlay() const noexcept {
    return m_valid && !m_overlayLayer.isNull() && m_overlayLayer.size() == m_canvasSize;
}

QSize FrameScene::canvasSize() const noexcept { return m_canvasSize; }

FrameScene FrameSceneBuilder::build(QImage sourceFrame, qint64 compositionTimeNs, QImage overlayLayer) {
    return {std::move(sourceFrame), compositionTimeNs, std::move(overlayLayer)};
}

} // namespace ariadshot::media
