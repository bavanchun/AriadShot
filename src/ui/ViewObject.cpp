// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewObject.h"

#include "ui/AccessibleChildren.h"
#include "ui/ViewRoot.h"

#include <QAccessibleEvent>
#include <QAccessibleInterface>

#include <memory>
#include <vector>

namespace ariadshot::ui {

namespace {

// Presents a view object to the accessibility bridges: its name, its role, where it is on the screen, its place under
// its parent and its children.
class ViewObjectAccessible final : public QAccessibleInterface {
  public:
    explicit ViewObjectAccessible(ViewObject& view) : m_view(view) {}

    bool isValid() const override { return true; }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int x, int y) const override {
        return detail::accessibleChildAt(m_view.children(), x, y);
    }
    QAccessibleInterface* parent() const override { return m_view.accessibleParent(); }
    QAccessibleInterface* child(int index) const override { return detail::accessibleChild(m_view.children(), index); }
    int childCount() const override { return static_cast<int>(m_view.children().size()); }
    int indexOfChild(const QAccessibleInterface* child) const override {
        return detail::accessibleIndexOfChild(m_view.children(), child);
    }
    QString text(QAccessible::Text text) const override {
        return text == QAccessible::Name ? m_view.accessibleName() : QString();
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return m_view.screenRect(); }
    QAccessible::Role role() const override { return m_view.accessibleRole(); }
    QAccessible::State state() const override { return {}; }

  private:
    ViewObject& m_view;
};

} // namespace

ViewObject::~ViewObject() {
    if (m_parent != nullptr) {
        if (ViewRoot* owner = root()) {
            owner->leftTheTree(*this);
            owner->addDamage(m_lastPaintBounds.isEmpty() ? m_geometry : m_lastPaintBounds);
        }
        std::erase(m_parent->m_children, this);
        m_parent = nullptr;
    }
    for (ViewObject* child : m_children) {
        child->m_parent = nullptr;
    }
    if (m_accessibleId != 0) {
        QAccessible::deleteAccessibleInterface(m_accessibleId);
    }
}

void ViewObject::setGeometry(QRectF geometry) {
    update(paintBounds());
    m_geometry = geometry;
    m_lastPaintBounds = paintBounds();
    update(m_lastPaintBounds);
}

ViewObject* ViewObject::hitTest(QPointF point) { return geometry().contains(point) ? this : nullptr; }

bool ViewObject::addChild(ViewObject& child) {
    for (const ViewObject* ancestor = this; ancestor != nullptr; ancestor = ancestor->m_parent) {
        if (ancestor == &child) {
            return false; // a cycle would never end at a root
        }
    }
    if (child.m_root != nullptr) {
        return false;
    }
    if (child.m_parent == this) {
        return true;
    }
    if (child.m_parent != nullptr) {
        child.m_parent->removeChild(child);
    }
    m_children.push_back(&child);
    child.m_parent = this;
    child.m_lastPaintBounds = child.paintBounds();
    child.update(child.m_lastPaintBounds);
    child.announce(QAccessible::ObjectCreated);
    return true;
}

bool ViewObject::removeChild(ViewObject& child) {
    if (child.m_parent != this) {
        return false;
    }
    ViewRoot* owner = root();
    const bool wasInTree = owner != nullptr;
    const QRectF damage = child.paintBounds();

    std::erase(m_children, &child);
    child.m_parent = nullptr;

    if (owner != nullptr) {
        owner->leftTheTree(child);
        owner->addDamage(damage);
    }
    if (wasInTree) {
        child.announce(QAccessible::ObjectDestroyed);
    }
    return true;
}

QAccessibleInterface* ViewObject::accessible() {
    if (m_accessibleId == 0) {
        // The registry owns the interface from here on; ~ViewObject gives it back by id.
        m_accessibleId =
            QAccessible::registerAccessibleInterface(std::make_unique<ViewObjectAccessible>(*this).release());
    }
    return QAccessible::accessibleInterface(m_accessibleId);
}

QAccessibleInterface* ViewObject::accessibleParent() const {
    if (m_parent != nullptr) {
        return m_parent->accessible();
    }
    return m_root != nullptr ? m_root->accessible() : nullptr;
}

QRect ViewObject::screenRect() const {
    const QRect area = geometry().toAlignedRect();
    const ViewRoot* owner = root();
    return owner != nullptr ? owner->toScreen(area) : area;
}

void ViewObject::update(QRectF damage) {
    if (ViewRoot* owner = root()) {
        owner->addDamage(damage);
    }
}

ViewRoot* ViewObject::root() const {
    const ViewObject* top = this;
    while (top->m_parent != nullptr) {
        top = top->m_parent;
    }
    return top->m_root;
}

void ViewObject::announce(QAccessible::Event event) {
    // Interfaces are made on demand, so nothing is made while no assistive technology listens.
    if (!QAccessible::isActive()) {
        return;
    }
    if (event != QAccessible::ObjectDestroyed && root() == nullptr) {
        return;
    }
    if (QAccessibleInterface* node = accessible()) {
        QAccessibleEvent change(node, event);
        QAccessible::updateAccessibility(&change);
    }
}

} // namespace ariadshot::ui
