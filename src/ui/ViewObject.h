// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAccessible>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QtGlobal>

#include <optional>

class QEvent;
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
    // Moves or resizes the view; the old and the new area are damaged.
    void setGeometry(QRectF geometry);

    // Paints into the chrome layer. The root calls it for every repaint, clipped to the damage, so a view may paint
    // outside geometry() (a border, a shadow); whoever changes such pixels reports them with update().
    virtual void paint(QPainter& painter) = 0;

    // The view object that receives a pointer event at the point: this one, a descendant, or nullptr to let the view
    // behind take it. The default accepts the points inside geometry().
    virtual ViewObject* hitTest(QPointF point);
    virtual void hoverChanged(bool /*hovered*/) {}
    // A press makes the view that hitTest() chose the owner of the pointer until every button is released: its moves
    // and its release come to it wherever the pointer is. Key and focus events reach the root's focus owner, input
    // method events its input method owner. Positions are in surface points.
    virtual void handleEvent(const QEvent& /*event*/) {}

    // The cursor over the point; nullopt hides it because the canvas draws its own.
    [[nodiscard]] virtual std::optional<Qt::CursorShape> cursorAt(QPointF /*point*/) const { return Qt::ArrowCursor; }
    [[nodiscard]] virtual QString toolTip() const { return {}; }

    // Every view object is accessible: it names itself and gives its role. accessible() wraps the two in a Qt
    // interface that the QAccessible registry owns; a view with more to say (text, value) overrides it.
    [[nodiscard]] virtual QString accessibleName() const = 0;
    [[nodiscard]] virtual QAccessible::Role accessibleRole() const = 0;
    virtual QAccessibleInterface* accessible();

    // Marks an area as changed; the held-by root hands it to the host with the next takeDamage().
    void update(QRectF damage);

  private:
    friend class ViewRoot;

    QRectF m_geometry;
    ViewRoot* m_root = nullptr;
    QAccessible::Id m_accessibleId = 0;
};

} // namespace ariadshot::ui
