// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewObject.h"

#include "ui/ViewRoot.h"

#include <QAccessibleInterface>

#include <memory>

namespace ariadshot::ui {

namespace {

// Presents a view object to the accessibility bridges: its name, its role, and where it is on the screen, as a child of
// the root that holds it.
class ViewObjectAccessible final : public QAccessibleInterface {
  public:
    explicit ViewObjectAccessible(const ViewObject& view) : m_view(view) {}

    bool isValid() const override { return true; }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    QAccessibleInterface* parent() const override { return m_view.accessibleParent(); }
    QAccessibleInterface* child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QString text(QAccessible::Text text) const override {
        return text == QAccessible::Name ? m_view.accessibleName() : QString();
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return m_view.screenRect(); }
    QAccessible::Role role() const override { return m_view.accessibleRole(); }
    QAccessible::State state() const override { return {}; }

  private:
    const ViewObject& m_view;
};

} // namespace

ViewObject::~ViewObject() {
    if (m_accessibleId != 0) {
        QAccessible::deleteAccessibleInterface(m_accessibleId);
    }
}

void ViewObject::setGeometry(QRectF geometry) {
    update(m_geometry);
    m_geometry = geometry;
    update(m_geometry);
}

ViewObject* ViewObject::hitTest(QPointF point) { return geometry().contains(point) ? this : nullptr; }

QAccessibleInterface* ViewObject::accessible() {
    if (m_accessibleId == 0) {
        // The registry owns the interface from here on; ~ViewObject gives it back by id.
        m_accessibleId =
            QAccessible::registerAccessibleInterface(std::make_unique<ViewObjectAccessible>(*this).release());
    }
    return QAccessible::accessibleInterface(m_accessibleId);
}

QAccessibleInterface* ViewObject::accessibleParent() const {
    return m_root != nullptr ? m_root->accessible() : nullptr;
}

QRect ViewObject::screenRect() const {
    const QRect area = geometry().toAlignedRect();
    return m_root != nullptr ? m_root->toScreen(area) : area;
}

void ViewObject::update(QRectF damage) {
    if (m_root != nullptr) {
        m_root->addDamage(damage);
    }
}

} // namespace ariadshot::ui
