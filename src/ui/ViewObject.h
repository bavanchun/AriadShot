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
#include <vector>

class QEvent;
class QInputMethodQueryEvent;
class QPainter;

namespace ariadshot::ui {

class ViewObjectAccessible;
class ViewRoot;
template <typename T> class BasicViewRef;

// One view of a surface: a ported MacShot view or a piece of chrome. Everything a view sees is in the surface's points:
// its geometry, the positions of the events it receives, the damage it reports and what it paints. A view object is
// held by one ViewRoot, which hit-tests it, delivers events to it and paints it into the chrome layer; a view that is
// not held by a root is the child of one that is. Thread: GUI.
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
    // behind take it. A descendant is a child of this view (addChild()). It must not change the tree. The default
    // accepts the points inside geometry().
    virtual ViewObject* hitTest(QPointF point);
    virtual void hoverChanged(bool /*hovered*/) {}
    // A press makes the view that hitTest() chose the owner of the pointer until every button is released: its moves
    // and its release come to it wherever the pointer is. Key and focus events reach the root's focus owner, input
    // method events its input method owner. Positions are in surface points.
    virtual void handleEvent(const QEvent& /*event*/) {}
    // Called before a pointer press is dispatched, allowing views such as popovers, menus, and the text
    // control to dismiss or commit when an outside press occurs without swallowing the event.
    virtual void rootPointerPressed(QPointF /*point*/) {}
    // Answers the input method's queries while this view is the root's input method owner: sets the value of each of
    // query.queries() with setValue(). Rectangles are in surface points. The default answers nothing.
    virtual void inputMethodQuery(QInputMethodQueryEvent& /*query*/) {}

    // The cursor over the point; nullopt hides it because the canvas draws its own.
    [[nodiscard]] virtual std::optional<Qt::CursorShape> cursorAt(QPointF /*point*/) const { return Qt::ArrowCursor; }
    [[nodiscard]] virtual QString toolTip() const { return {}; }

    // Declares a view a child of this one, in front of the children it has: the view a container's hitTest() hands out
    // is its child. The child reports this view's interface as its accessible parent, and its damage and its screen
    // position go through the root that holds this view. Nothing is owned: the holder keeps its children alive, and
    // either side may be destroyed first. Adding damages what the child paints and tells assistive technology. Returns
    // false, and changes nothing, for this view itself, one of its ancestors and a view a root holds; a child of
    // another view moves.
    [[nodiscard]] bool addChild(ViewObject& child);
    // Takes a child out again; false when the view is not a child of this one.
    bool removeChild(ViewObject& child);
    [[nodiscard]] const std::vector<ViewObject*>& children() const { return m_children; }

    // Every view object is accessible: it names itself and gives its role. accessible() wraps the two in a Qt
    // interface that the QAccessible registry owns, listing the children; a view with more to say (text, value)
    // overrides it and reports accessibleParent() and screenRect() like the default does. An override returns an
    // interface registered with QAccessible::registerAccessibleInterface; the registry owns it and ~ViewObject
    // deletes it by id.
    [[nodiscard]] virtual QString accessibleName() const = 0;
    [[nodiscard]] virtual QAccessible::Role accessibleRole() const = 0;
    virtual QAccessibleInterface* accessible();
    // The parent in the accessible tree: the interface of the view this one is a child of, else of the root that holds
    // it, nullptr when there is neither.
    [[nodiscard]] QAccessibleInterface* accessibleParent() const;
    // geometry() in screen coordinates, mapped by the host the root is attached to; the surface's own when it is not.
    [[nodiscard]] QRect screenRect() const;

    // Marks an area as changed; the root that holds the view, or its parent, hands it to the host with the next
    // takeDamage().
    void update(QRectF damage);

    [[nodiscard]] std::weak_ptr<void> lifetimeToken() const noexcept { return m_lifetime; }

  protected:
    [[nodiscard]] bool isDying() const noexcept { return m_dying; }
    QAccessible::Id m_accessibleId = 0;

  private:
    friend class ViewRoot;
    friend class ViewObjectAccessible;

    // The root this view belongs to, through its parents; nullptr when it is in no tree.
    [[nodiscard]] ViewRoot* root() const;
    // Detaches this view from its parent: removes from parent's children, clears m_parent, and updates the old root.
    void detachFromParent();
    // Tells assistive technology that this view entered or left the tree, if any of it is listening.
    void announce(QAccessible::Event event);

    QRectF m_geometry;
    ViewRoot* m_root = nullptr; // the holder of a top-level view
    ViewObject* m_parent = nullptr;
    std::vector<ViewObject*> m_children;
    // Lets whoever points at a view without owning it (the root's owners of pointer, focus and input method, and the
    // hovered view) see that it is gone: they hold a weak reference and never call a view whose token has expired.
    std::shared_ptr<void> m_lifetime = std::make_shared<char>();
    QRectF m_lastPaintBounds;
    bool m_dying = false;
    bool m_removing = false;
};

// A non-owning reference to a view object that detects when the view has been destroyed.
template <typename T> class BasicViewRef {
  public:
    BasicViewRef() = default;
    /* implicit */ BasicViewRef(T* view)
        : m_view(view), m_alive(view != nullptr ? view->lifetimeToken() : std::weak_ptr<void>{}) {}
    [[nodiscard]] T* get() const { return m_alive.expired() ? nullptr : m_view; }
    [[nodiscard]] bool isExpired() const { return m_alive.expired(); }
    [[nodiscard]] explicit operator bool() const { return get() != nullptr; }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    friend bool operator==(const BasicViewRef& a, const BasicViewRef& b) { return a.get() == b.get(); }
    friend bool operator==(const BasicViewRef& a, std::nullptr_t) { return a.get() == nullptr; }

  private:
    T* m_view = nullptr;
    std::weak_ptr<void> m_alive;
};

using ViewRef = BasicViewRef<ViewObject>;
using ConstViewRef = BasicViewRef<const ViewObject>;

} // namespace ariadshot::ui
