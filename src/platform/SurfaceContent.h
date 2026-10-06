// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "platform/SurfaceHost.h"

#include <QPoint>
#include <QRegion>
#include <QtGlobal>

#include <functional>
#include <optional>
#include <vector>

class QAccessibleInterface;
class QEvent;
class QImage;
class QInputMethodQueryEvent;

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

    // Pointer, wheel, tablet, key, input method and focus events from the host. An input method query is not routed
    // here: it needs an answer, which a const event cannot carry.
    virtual void dispatch(const QEvent& event) = 0;

    // Answers an input method query (surrounding text, cursor position and so on): sets the value of each of
    // query.queries() with setValue(). Rectangles are in the surface's points.
    virtual void inputMethodQuery(QInputMethodQueryEvent& query) = 0;

    // The content's interface in the accessible tree: a container whose children are its views. The accessibility
    // registry owns it; it lives as long as the content. The host adds it to its own window's interface as a child.
    [[nodiscard]] virtual QAccessibleInterface* accessible() = 0;

    // Tells the content where it hangs in the accessible tree: the host's interface, which is the parent of
    // accessible(), and the mapping from a point of the surface to a screen coordinate. Interfaces report rectangles in
    // screen coordinates, and a surface can move, so the content calls the mapping each time it is asked. The host
    // detaches (nullptr and an empty function) before it destroys the parent. Without a host the surface's own
    // coordinates stand.
    virtual void attachAccessible(QAccessibleInterface* parent, std::function<QPoint(QPoint)> surfaceToScreen) = 0;

    // The cursor for the pointer's position; nullopt hides it (the canvas draws its own).
    [[nodiscard]] virtual std::optional<Qt::CursorShape> cursor() const = 0;

    [[nodiscard]] virtual KeyboardInteractivity keyboard() const = 0;
};

} // namespace ariadshot::platform
