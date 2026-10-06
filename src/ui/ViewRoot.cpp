// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewRoot.h"

#include "render/CanonicalImage.h"
#include "ui/ViewObject.h"

#include <QAccessibleInterface>
#include <QEnterEvent>
#include <QEvent>
#include <QInputMethodQueryEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <ranges>
#include <utility>

namespace ariadshot::ui {

// The root's interface in the accessible tree: a container of its views' interfaces under the host's interface.
class ViewRoot::Accessible final : public QAccessibleInterface {
  public:
    explicit Accessible(ViewRoot& root) : m_root(root) {}

    bool isValid() const override { return true; }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int x, int y) const override {
        for (const auto& view : std::views::reverse(m_root.m_views)) {
            if (view->screenRect().contains(x, y)) {
                return view->accessible();
            }
        }
        return nullptr;
    }
    QAccessibleInterface* parent() const override { return m_root.m_accessibleParent; }
    QAccessibleInterface* child(int index) const override {
        const bool held = index >= 0 && std::cmp_less(index, m_root.m_views.size());
        return held ? m_root.m_views[static_cast<std::size_t>(index)]->accessible() : nullptr;
    }
    int childCount() const override { return static_cast<int>(m_root.m_views.size()); }
    int indexOfChild(const QAccessibleInterface* child) const override {
        const auto held =
            std::ranges::find_if(m_root.m_views, [child](const auto& view) { return view->accessible() == child; });
        return held != m_root.m_views.end() ? static_cast<int>(held - m_root.m_views.begin()) : -1;
    }
    QString text(QAccessible::Text) const override { return {}; }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return m_root.toScreen(m_root.m_surface); }
    QAccessible::Role role() const override { return QAccessible::Pane; }
    QAccessible::State state() const override { return {}; }

  private:
    ViewRoot& m_root;
};

ViewRoot::~ViewRoot() {
    if (m_accessibleId != 0) {
        QAccessible::deleteAccessibleInterface(m_accessibleId);
    }
}

// The buffer rounds up: rounding to nearest can leave the last row or column of the surface without pixels.
void ViewRoot::resize(QSize size, qreal scale) {
    m_surface = QRect(QPoint(), size);
    m_chrome = render::makeCanonicalImage(QSize(qCeil(size.width() * scale), qCeil(size.height() * scale)), scale);
    m_damage = m_surface;
}

ViewObject& ViewRoot::addView(std::unique_ptr<ViewObject> view) {
    ViewObject& added = *m_views.emplace_back(std::move(view));
    added.m_root = this;
    addDamage(added.geometry());
    return added;
}

std::unique_ptr<ViewObject> ViewRoot::removeView(ViewObject& view) {
    const auto held =
        std::ranges::find_if(m_views, [&view](const auto& candidate) { return candidate.get() == &view; });
    if (held == m_views.end()) {
        return nullptr;
    }
    std::unique_ptr<ViewObject> removed = std::move(*held);
    m_views.erase(held);
    removed->m_root = nullptr;
    addDamage(removed->geometry());
    if (m_focusOwner == removed.get()) {
        m_focusOwner = nullptr;
    }
    if (m_inputMethodOwner == removed.get()) {
        m_inputMethodOwner = nullptr;
    }
    if (m_pointerOwner.holder == removed.get()) {
        m_pointerOwner = {};
    }
    // The hovered view object may be the removed one or part of it; it learns that the pointer left before it goes.
    if (m_hovered != nullptr) {
        updateHover(true);
    }
    return removed;
}

void ViewRoot::setCanonicalLayers(std::vector<const QImage*> layers) {
    m_canonicalLayers = std::move(layers);
    m_damage = m_surface;
}

std::optional<Qt::CursorShape> ViewRoot::cursorAt(QPointF point) const {
    const ViewObject* view = viewAt(point);
    return view != nullptr ? view->cursorAt(point) : std::optional<Qt::CursorShape>(Qt::ArrowCursor);
}

std::vector<const QImage*> ViewRoot::presentationLayers() const {
    std::vector<const QImage*> layers = m_canonicalLayers;
    if (!m_chrome.isNull()) {
        layers.push_back(&m_chrome);
    }
    return layers;
}

QRegion ViewRoot::takeDamage() {
    const QRegion damage = std::exchange(m_damage, QRegion());
    if (!damage.isEmpty()) {
        paintChrome(damage);
    }
    return damage;
}

void ViewRoot::dispatch(const QEvent& event) {
    switch (event.type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        dispatchPointer(static_cast<const QSinglePointEvent&>(event));
        break;
    case QEvent::Enter:
        m_pointer = static_cast<const QEnterEvent&>(event).position();
        updateHover(true);
        break;
    case QEvent::Leave:
        updateHover(false);
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
        if (m_focusOwner != nullptr) {
            m_focusOwner->handleEvent(event);
        }
        break;
    case QEvent::InputMethod:
        if (m_inputMethodOwner != nullptr) {
            m_inputMethodOwner->handleEvent(event);
        }
        break;
    default:
        break;
    }
}

void ViewRoot::inputMethodQuery(QInputMethodQueryEvent& query) {
    if (m_inputMethodOwner != nullptr) {
        m_inputMethodOwner->inputMethodQuery(query);
    } else {
        query.setValue(Qt::ImEnabled, false);
    }
}

QAccessibleInterface* ViewRoot::accessible() {
    if (m_accessibleId == 0) {
        // The registry owns the interface from here on; ~ViewRoot gives it back by id.
        m_accessibleId = QAccessible::registerAccessibleInterface(std::make_unique<Accessible>(*this).release());
    }
    return QAccessible::accessibleInterface(m_accessibleId);
}

void ViewRoot::attachAccessible(QAccessibleInterface* parent, std::function<QPoint(QPoint)> surfaceToScreen) {
    m_accessibleParent = parent;
    m_surfaceToScreen = std::move(surfaceToScreen);
}

// Damage is whole points, rounded outwards so that it never covers less than the change.
void ViewRoot::addDamage(QRectF area) { m_damage += area.toAlignedRect() & m_surface; }

ViewRoot::Hit ViewRoot::hitAt(QPointF point) const {
    for (const auto& view : std::views::reverse(m_views)) {
        if (ViewObject* hit = view->hitTest(point)) {
            return {.holder = view.get(), .view = hit};
        }
    }
    return {};
}

void ViewRoot::dispatchPointer(const QSinglePointEvent& event) {
    const QEvent::Type type = event.type();
    const bool press =
        type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick || type == QEvent::TabletPress;
    const bool release = type == QEvent::MouseButtonRelease || type == QEvent::TabletRelease;
    const bool held = event.buttons() != Qt::NoButton;
    m_pointer = event.position();
    if (!held && !release) {
        m_pointerOwner = {}; // no button is held, so no drag is going on
    }
    updateHover(true);
    // Hover callbacks can change the tree, so the target is found only now. A wheel turns whatever is under the
    // pointer.
    const bool owned = m_pointerOwner.view != nullptr && type != QEvent::Wheel;
    const Hit target = owned ? m_pointerOwner : hitAt(m_pointer);
    if (target.view == nullptr) {
        return;
    }
    if (press && m_pointerOwner.view == nullptr) {
        m_pointerOwner = target;
    } else if (release && !held) {
        m_pointerOwner = {};
    }
    target.view->handleEvent(event);
}

// m_hovered is updated before each callback runs. A callback can remove views, including the one it was called on or
// the one that is to be entered, so the view to enter is found again after every callback.
void ViewRoot::updateHover(bool pointerInside) {
    for (;;) {
        ViewObject* target = pointerInside ? viewAt(m_pointer) : nullptr;
        if (target == m_hovered) {
            return;
        }
        if (m_hovered != nullptr) {
            std::exchange(m_hovered, nullptr)->hoverChanged(false);
            continue;
        }
        m_hovered = target;
        target->hoverChanged(true);
    }
}

void ViewRoot::paintChrome(const QRegion& damage) {
    if (m_chrome.isNull()) {
        return;
    }
    QPainter painter(&m_chrome);
    painter.setClipRegion(damage);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(m_surface, Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // Every view paints, under the clip: what a view draws outside its geometry is cleared with the damage and has to
    // come back with it.
    for (const auto& view : m_views) {
        painter.save();
        view->paint(painter);
        painter.restore();
    }
}

QRect ViewRoot::toScreen(QRect area) const {
    return m_surfaceToScreen ? QRect(m_surfaceToScreen(area.topLeft()), area.size()) : area;
}

} // namespace ariadshot::ui
