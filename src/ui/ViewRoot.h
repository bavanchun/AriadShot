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

    // Takes a view object, in front of the ones added before, and returns it; a view that is the child of another
    // leaves it. The area it paints is damaged and assistive technology is told.
    ViewObject& addView(std::unique_ptr<ViewObject> view);
    // Gives a view object back, or returns nullptr when this root does not hold it. The area it painted is damaged and
    // assistive technology is told. A gesture that the view or part of it took the press of is cancelled. The caller
    // clears the focus and input method owners that are part of the view first.
    std::unique_ptr<ViewObject> removeView(ViewObject& view);

    // The canonical layers, bottom first. The canvas owns the images and reports changes with ViewObject::update().
    void setCanonicalLayers(std::vector<const QImage*> layers);

    // The view object that receives key and focus events, and the one that receives input method events (not owned;
    // nullptr for none). One that is destroyed stops being the owner.
    void setFocusOwner(ViewObject* view) { m_focusOwner = Ref(view); }
    void setInputMethodOwner(ViewObject* view) { m_inputMethodOwner = Ref(view); }
    void setKeyboard(platform::KeyboardInteractivity mode) { m_keyboard = mode; }

    // The cursor over a point: the front view object's choice, the arrow when none is there.
    [[nodiscard]] std::optional<Qt::CursorShape> cursorAt(QPointF point) const;

    // SurfaceContent. Pointer, wheel and tablet events go to the view object under the pointer, except that a press
    // makes the view object it landed on the owner of the pointer until every button is released: its moves and its
    // release go to it wherever the pointer is. A gesture whose owner is removed or destroyed is cancelled: the rest of
    // it, up to the release, goes to no view, not to the one behind, which never saw the press. A move with no button
    // held, or the first button of a new press, ends a gesture whose release was lost. Key and focus events go to the
    // focus owner; input method events and queries to the input method owner, and a query with no owner answers that
    // input is disabled. QEvent::Enter must be a QEnterEvent. The accessible interface is a container of the views'
    // interfaces, back to front.
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

    // A view object this root does not own, such as a descendant that a holder hands out through hitTest() or the focus
    // owner. It can be destroyed while the root remembers it, so a destroyed one reads as nullptr and is never called.
    class Ref {
      public:
        Ref() = default;
        explicit Ref(ViewObject* view);
        [[nodiscard]] ViewObject* get() const { return m_alive.expired() ? nullptr : m_view; }

      private:
        ViewObject* m_view = nullptr;
        std::weak_ptr<void> m_alive;
    };

    // What a point hits: the view object this root holds and what its hitTest chose, the holder or a descendant. It is
    // used at once, before anything can change the tree.
    struct Hit {
        ViewObject* holder = nullptr;
        ViewObject* view = nullptr;
    };

    // The view a press landed on, for as long as the gesture lasts: holder is nullptr when none is going on. The holder
    // is held by this root; the view may be a descendant that its holder destroys. When the view or its holder leaves
    // the tree the gesture stays, with no view to go to.
    struct PointerOwner {
        ViewObject* holder = nullptr;
        Ref view;
    };

    void addDamage(QRectF area);
    [[nodiscard]] Hit hitAt(QPointF point) const;
    [[nodiscard]] ViewObject* viewAt(QPointF point) const { return hitAt(point).view; }
    void dispatchPointer(const QSinglePointEvent& event);
    void leftTheTree(const ViewObject& view);
    void updateHover(bool pointerInside);
    void paintChrome(const QRegion& damage);
    [[nodiscard]] QRect toScreen(QRect area) const;

    std::vector<std::unique_ptr<ViewObject>> m_views; // back to front
    std::vector<const QImage*> m_canonicalLayers;
    QImage m_chrome;
    QRect m_surface;
    QRegion m_damage;
    QPointF m_pointer;
    Ref m_hovered;
    PointerOwner m_pointerOwner;
    Ref m_focusOwner;
    Ref m_inputMethodOwner;
    platform::KeyboardInteractivity m_keyboard = platform::KeyboardInteractivity::None;
    QAccessibleInterface* m_accessibleParent = nullptr;
    std::function<QPoint(QPoint)> m_surfaceToScreen;
    QAccessible::Id m_accessibleId = 0;
};

} // namespace ariadshot::ui
