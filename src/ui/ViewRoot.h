// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "platform/SurfaceContent.h"

#include <QAccessible>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QSinglePointEvent;

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

    // SurfaceContent. Pointer, wheel and tablet events go to the view object under the pointer, except that a press
    // makes the view object it landed on the owner of the pointer until every button is released: its moves and its
    // release go to it wherever the pointer is (a move with no button held ends a drag whose release was lost). Key
    // and focus events go to the focus owner; input method events and queries to the input method owner, and a query
    // with no owner answers that input is disabled. QEvent::Enter must be a QEnterEvent. The accessible interface is a
    // container of the views' interfaces, back to front.
    [[nodiscard]] std::vector<const QImage*> presentationLayers() const override;
    QRegion takeDamage() override;
    void dispatch(const QEvent& event) override;
    void inputMethodQuery(QInputMethodQueryEvent& query) override;
    [[nodiscard]] std::optional<Qt::CursorShape> cursor() const override { return cursorAt(m_pointer); }
    [[nodiscard]] platform::KeyboardInteractivity keyboard() const override { return m_keyboard; }
    [[nodiscard]] QAccessibleInterface* accessible() override;
    void attachAccessible(QAccessibleInterface* parent, std::function<QPoint(QPoint)> surfaceToScreen) override;

  private:
    friend class ViewObject;
    class Accessible;

    // What a point hits: the view object this root holds and what its hitTest chose, the holder or a descendant.
    struct Hit {
        ViewObject* holder = nullptr;
        ViewObject* view = nullptr;
    };

    void addDamage(QRectF area);
    [[nodiscard]] Hit hitAt(QPointF point) const;
    [[nodiscard]] ViewObject* viewAt(QPointF point) const { return hitAt(point).view; }
    void dispatchPointer(const QSinglePointEvent& event);
    void updateHover(bool pointerInside);
    void paintChrome(const QRegion& damage);
    [[nodiscard]] QRect toScreen(QRect area) const;

    std::vector<std::unique_ptr<ViewObject>> m_views; // back to front
    std::vector<const QImage*> m_canonicalLayers;
    QImage m_chrome;
    QRect m_surface;
    QRegion m_damage;
    QPointF m_pointer;
    ViewObject* m_hovered = nullptr;
    Hit m_pointerOwner;
    ViewObject* m_focusOwner = nullptr;
    ViewObject* m_inputMethodOwner = nullptr;
    platform::KeyboardInteractivity m_keyboard = platform::KeyboardInteractivity::None;
    QAccessibleInterface* m_accessibleParent = nullptr;
    std::function<QPoint(QPoint)> m_surfaceToScreen;
    QAccessible::Id m_accessibleId = 0;
};

} // namespace ariadshot::ui
