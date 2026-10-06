// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QSize>
#include <QtGlobal>

namespace ariadshot::media {

// Immutable snapshot of a studio scene for rendering.
// Represents a single frame at a specific composition time with source frame and overlays.
// macshot/Capture/VideoSceneRenderer.swift:150-235@b4d4f3a
// macshot/Capture/VideoSceneBuilder.swift@b4d4f3a
// Thread: any
class FrameScene {
  public:
    FrameScene() = default;
    FrameScene(QImage sourceFrame, qint64 compositionTimeNs, QImage overlayLayer = {});

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] const QImage& sourceFrame() const noexcept;
    [[nodiscard]] qint64 compositionTime() const noexcept;
    [[nodiscard]] const QImage& overlayLayer() const noexcept;
    [[nodiscard]] bool hasOverlay() const noexcept;
    [[nodiscard]] QSize canvasSize() const noexcept;

  private:
    QImage m_sourceFrame;
    qint64 m_compositionTimeNs = 0;
    QImage m_overlayLayer;
    QSize m_canvasSize;
    bool m_valid = false;
};

// Builds immutable FrameScene snapshots for composition.
// Thread: any
class FrameSceneBuilder {
  public:
    [[nodiscard]] static FrameScene build(QImage sourceFrame, qint64 compositionTimeNs, QImage overlayLayer = {});
};

} // namespace ariadshot::media
