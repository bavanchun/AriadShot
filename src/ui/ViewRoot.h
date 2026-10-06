// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "platform/SurfaceContent.h"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>

#include <memory>
#include <optional>
#include <vector>

namespace ariadshot::ui {

class ViewObject;

// The content of one hosted surface or embedding window. It holds the view objects front to back, routes the host's
// events to them, collects their damage and repaints the chrome layer over it. The canonical layers come from the
// canvas (made by render/); the root puts the chrome layer on top. All coordinates are the surface's points. Thread:
// GUI.
class ViewRoot : public platform::SurfaceContent {
  public:
    ViewRoot() = default;
    ~ViewRoot() override;

    // Sets the surface size in points and its scale; reallocates the chrome layer and damages the whole surface.
    void resize(QSize size, qreal scale);

    // Takes a view object, in front of the ones added before, and returns it.
    ViewObject& addView(std::unique_ptr<ViewObject> view);
    // Gives a view object back, or returns nullptr when this root does not hold it. The caller clears the focus and
    // input method owners that are the view or part of it first.
    std::unique_ptr<ViewObject> removeView(ViewObject& view);

    // The canonical layers, bottom first. The canvas owns the images and reports changes with ViewObject::update().
    void setCanonicalLayers(std::vector<const QImage*> layers);

    // The view object that receives key and focus events, and the one that receives input method events (not owned;
    // nullptr for none).
    void setFocusOwner(ViewObject* view) { m_focusOwner = view; }
    void setInputMethodOwner(ViewObject* view) { m_inputMethodOwner = view; }
    void setKeyboard(platform::KeyboardInteractivity mode) { m_keyboard = mode; }

    // The cursor over a point: the front view object's choice, the arrow when none is there.
    [[nodiscard]] std::optional<Qt::CursorShape> cursorAt(QPointF point) const;

    // SurfaceContent. Pointer, wheel and tablet events go to the view object under the pointer; key and focus events
    // to the focus owner; input method events to the input method owner. QEvent::Enter must be a QEnterEvent.
    [[nodiscard]] std::vector<const QImage*> presentationLayers() const override;
    QRegion takeDamage() override;
    void dispatch(const QEvent& event) override;
    [[nodiscard]] std::optional<Qt::CursorShape> cursor() const override { return cursorAt(m_pointer); }
    [[nodiscard]] platform::KeyboardInteractivity keyboard() const override { return m_keyboard; }

  private:
    friend class ViewObject;

    void addDamage(QRectF area);
    [[nodiscard]] ViewObject* viewAt(QPointF point) const;
    void setHovered(ViewObject* view);
    void paintChrome(const QRegion& damage);

    std::vector<std::unique_ptr<ViewObject>> m_views; // back to front
    std::vector<const QImage*> m_canonicalLayers;
    QImage m_chrome;
    QRect m_surface;
    QRegion m_damage;
    QPointF m_pointer;
    ViewObject* m_hovered = nullptr;
    ViewObject* m_focusOwner = nullptr;
    ViewObject* m_inputMethodOwner = nullptr;
    platform::KeyboardInteractivity m_keyboard = platform::KeyboardInteractivity::None;
};

} // namespace ariadshot::ui
