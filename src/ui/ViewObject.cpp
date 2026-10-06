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

// Presents a view object to the accessibility bridges: its name, its role, where it is on the screen, its place under
// its parent and its children.
class ViewObjectAccessible final : public QAccessibleInterface {
  public:
    explicit ViewObjectAccessible(ViewObject& view)
        : m_view(view), m_cachedRole(view.accessibleRole()), m_cachedName(view.accessibleName()) {}

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
        if (text == QAccessible::Name) {
            if (!m_view.m_dying) {
                m_cachedName = m_view.accessibleName();
            }
            return m_cachedName;
        }
        return {};
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return m_view.screenRect(); }
    QAccessible::Role role() const override {
        if (!m_view.m_dying) {
            m_cachedRole = m_view.accessibleRole();
        }
        return m_cachedRole;
    }
    QAccessible::State state() const override { return {}; }

  private:
    ViewObject& m_view;
    mutable QAccessible::Role m_cachedRole;
    mutable QString m_cachedName;
};

ViewObject::~ViewObject() {
    m_dying = true;
    if (m_parent != nullptr) {
        if (m_accessibleId != 0 && !m_destroyedAnnounced) {
            announce(QAccessible::ObjectDestroyed);
        }
        if (ViewRoot* const owner = root()) {
            owner->leftTheTree(*this);
            owner->addDamage(m_lastPaintBounds.isEmpty() ? m_geometry : m_lastPaintBounds);
        }
        std::erase(m_parent->m_children, this);
        m_formerAccessibleParent = m_parent->accessible();
        m_parent = nullptr;
    } else if (m_root != nullptr) {
        if (m_accessibleId != 0 && !m_destroyedAnnounced) {
            announce(QAccessible::ObjectDestroyed);
        }
        m_root->leftTheTree(*this);
        m_root->addDamage(m_lastPaintBounds.isEmpty() ? m_geometry : m_lastPaintBounds);
        m_formerAccessibleParent = m_root->accessible();
        m_root = nullptr;
    }
    for (ViewObject* child : m_children) {
        child->m_parent = nullptr;
    }
    if (m_accessibleId != 0) {
        QAccessible::deleteAccessibleInterface(m_accessibleId);
        m_accessibleId = 0;
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
    if (child.m_root != nullptr && !child.m_removing) {
        return false;
    }
    if (child.m_parent == this) {
        return true;
    }
    const std::weak_ptr<void> alive = child.m_lifetime;
    const std::weak_ptr<void> selfAlive = m_lifetime;
    if (child.m_parent != nullptr) {
        if (!child.m_removing) {
            child.m_parent->removeChild(child);
        } else {
            std::erase(child.m_parent->m_children, &child);
            child.m_parent = nullptr;
        }
        if (alive.expired() || selfAlive.expired() || child.m_parent != nullptr ||
            (child.m_root != nullptr && !child.m_removing)) {
            return false;
        }
    }
    if (child.m_root != nullptr) {
        child.m_root = nullptr;
    }
    m_children.push_back(&child);
    child.m_parent = this;
    child.m_lastPaintBounds = child.paintBounds();
    child.update(child.m_lastPaintBounds);
    child.announce(QAccessible::ObjectCreated);
    return true;
}

bool ViewObject::removeChild(ViewObject& child) {
    if (child.m_parent != this || child.m_removing) {
        return false;
    }
    child.m_removing = true;
    const std::weak_ptr<void> alive = child.m_lifetime;
    const std::weak_ptr<void> selfAlive = m_lifetime;

    if (child.m_accessibleId != 0 && !child.m_destroyedAnnounced) {
        child.announce(QAccessible::ObjectDestroyed);
    }
    if (alive.expired() || selfAlive.expired() || child.m_parent != this) {
        if (!alive.expired()) {
            child.m_removing = false;
        }
        return true;
    }

    child.m_removing = false;
    ViewRoot* const owner = root();
    const QRectF damage = child.paintBounds();

    std::erase(m_children, &child);
    child.m_formerAccessibleParent = accessible();
    child.m_parent = nullptr;

    if (owner != nullptr) {
        owner->leftTheTree(child);
        owner->addDamage(damage);
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
    if (m_root != nullptr) {
        return m_root->accessible();
    }
    return m_formerAccessibleParent;
}

QRect ViewObject::screenRect() const {
    const QRect area = geometry().toAlignedRect();
    const ViewRoot* owner = root();
    return owner != nullptr ? owner->toScreen(area) : area;
}

void ViewObject::update(QRectF damage) {
    m_lastPaintBounds = paintBounds();
    if (ViewRoot* const owner = root()) {
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
    if (event == QAccessible::ObjectDestroyed) {
        m_destroyedAnnounced = true;
        if (m_accessibleId == 0) {
            return;
        }
        if (QAccessibleInterface* node = QAccessible::accessibleInterface(m_accessibleId)) {
            QAccessibleEvent change(node, event);
            QAccessible::updateAccessibility(&change);
        }
        return;
    }
    if (root() == nullptr) {
        return;
    }
    if (QAccessibleInterface* node = accessible()) {
        QAccessibleEvent change(node, event);
        QAccessible::updateAccessibility(&change);
    }
}

} // namespace ariadshot::ui
