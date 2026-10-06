// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "platform/SurfaceHost.h"

#include <QRegion>
#include <QtGlobal>

#include <optional>
#include <vector>

class QEvent;
class QImage;

namespace ariadshot::platform {

// What a surface shows. Implemented by ui::ViewRoot; platform never includes ui headers, so a host depends on this
// interface alone. Coordinates are the surface's points. Thread: GUI.
class SurfaceContent {
  public:
    SurfaceContent() = default;
    virtual ~SurfaceContent();
    Q_DISABLE_COPY_MOVE(SurfaceContent)

    // The canonical layers and the chrome layer, bottom first; the pointers stay valid until the content changes size.
    [[nodiscard]] virtual std::vector<const QImage*> presentationLayers() const = 0;

    // Returns the region that changed since the last call and makes the layers current over it.
    virtual QRegion takeDamage() = 0;

    // Pointer, wheel, tablet, key, input method and focus events from the host.
    virtual void dispatch(const QEvent& event) = 0;

    // The cursor for the pointer's position; nullopt hides it (the canvas draws its own).
    [[nodiscard]] virtual std::optional<Qt::CursorShape> cursor() const = 0;

    [[nodiscard]] virtual KeyboardInteractivity keyboard() const = 0;
};

} // namespace ariadshot::platform
