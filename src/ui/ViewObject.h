// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAccessible>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>

class QEvent;
class QInputMethodQueryEvent;
class QPainter;

namespace ariadshot::ui {

class ViewRoot;

// One view of a surface: a ported MacShot view or a piece of chrome. Everything a view sees is in the surface's points:
// its geometry, the positions of the events it receives, the damage it reports and what it paints. A view object is
// held by one ViewRoot, which hit-tests it, delivers events to it and paints it into the chrome layer. Thread: GUI.
class ViewObject {
  public:
    ViewObject() = default;
    virtual ~ViewObject();
    Q_DISABLE_COPY_MOVE(ViewObject)

    [[nodiscard]] virtual QRectF geometry() const { return m_geometry; }
    // Moves or resizes the view; what it painted and what it paints now are damaged.
    void setGeometry(QRectF geometry);
    // The area paint() draws in: the geometry, unless the view paints beyond it (a border, a shadow). The root clips
    // paint() to it and damages it when the view is added, removed or moved. A view whose reach changes without a
    // move reports the old and the new area with update().
    [[nodiscard]] virtual QRectF paintBounds() const { return geometry(); }

    // Paints into the chrome layer, within paintBounds(). The root calls it for every repaint, clipped to the damage.
    // It must not add or remove views.
    virtual void paint(QPainter& painter) = 0;

    // The view object that receives a pointer event at the point: this one, a descendant, or nullptr to let the view
    // behind take it. It must not change the tree. The default accepts the points inside geometry().
    virtual ViewObject* hitTest(QPointF point);
    virtual void hoverChanged(bool /*hovered*/) {}
    // A press makes the view that hitTest() chose the owner of the pointer until every button is released: its moves
    // and its release come to it wherever the pointer is. Key and focus events reach the root's focus owner, input
    // method events its input method owner. Positions are in surface points.
    virtual void handleEvent(const QEvent& /*event*/) {}
    // Answers the input method's queries while this view is the root's input method owner: sets the value of each of
    // query.queries() with setValue(). Rectangles are in surface points. The default answers nothing.
    virtual void inputMethodQuery(QInputMethodQueryEvent& /*query*/) {}

    // The cursor over the point; nullopt hides it because the canvas draws its own.
    [[nodiscard]] virtual std::optional<Qt::CursorShape> cursorAt(QPointF /*point*/) const { return Qt::ArrowCursor; }
    [[nodiscard]] virtual QString toolTip() const { return {}; }

    // Every view object is accessible: it names itself and gives its role. accessible() wraps the two in a Qt
    // interface that the QAccessible registry owns; a view with more to say (text, value) overrides it and reports
    // accessibleParent() and screenRect() like the default does.
    [[nodiscard]] virtual QString accessibleName() const = 0;
    [[nodiscard]] virtual QAccessible::Role accessibleRole() const = 0;
    virtual QAccessibleInterface* accessible();
    // The parent in the accessible tree: the interface of the root that holds this view, nullptr when none does.
    [[nodiscard]] QAccessibleInterface* accessibleParent() const;
    // geometry() in screen coordinates, mapped by the host the root is attached to; the surface's own when it is not.
    [[nodiscard]] QRect screenRect() const;

    // Marks an area as changed; the held-by root hands it to the host with the next takeDamage().
    void update(QRectF damage);

  private:
    friend class ViewRoot;

    QRectF m_geometry;
    ViewRoot* m_root = nullptr;
    QAccessible::Id m_accessibleId = 0;
    // Lets whoever points at a view without owning it (the root's owners of pointer, focus and input method, and the
    // hovered view) see that it is gone: they hold a weak reference and never call a view whose token has expired.
    std::shared_ptr<void> m_lifetime = std::make_shared<char>();
};

} // namespace ariadshot::ui
