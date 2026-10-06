// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewRoot.h"

#include "render/CanonicalImage.h"
#include "ui/AccessibleChildren.h"
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
        return detail::accessibleChildAt(m_root.m_views, x, y);
    }
    QAccessibleInterface* parent() const override { return m_root.m_accessibleParent; }
    QAccessibleInterface* child(int index) const override { return detail::accessibleChild(m_root.m_views, index); }
    int childCount() const override { return static_cast<int>(m_root.m_views.size()); }
    int indexOfChild(const QAccessibleInterface* child) const override {
        return detail::accessibleIndexOfChild(m_root.m_views, child);
    }
    QString text(QAccessible::Text) const override { return {}; }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return m_root.toScreen(m_root.m_surface); }
    QAccessible::Role role() const override { return QAccessible::Pane; }
    QAccessible::State state() const override { return {}; }

  private:
    ViewRoot& m_root;
};

ViewRoot::Ref::Ref(ViewObject* view) : m_view(view) {
    if (view != nullptr) {
        m_alive = view->m_lifetime;
    }
}

// The views are destroyed after the root's other members, and a view's destructor may still report damage or ask for
// its screen position; it has to find no root by then.
ViewRoot::~ViewRoot() {
    for (const auto& view : m_views) {
        view->m_root = nullptr;
    }
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
    while (view->m_parent != nullptr) {
        view->m_parent->removeChild(*view);
    }
    ViewObject& added = *m_views.emplace_back(std::move(view));
    added.m_root = this;
    added.m_lastPaintBounds = added.paintBounds();
    addDamage(added.m_lastPaintBounds);
    added.announce(QAccessible::ObjectCreated);
    return added;
}

std::unique_ptr<ViewObject> ViewRoot::removeView(ViewObject& view) {
    if (view.m_removing) {
        return nullptr;
    }
    auto held = std::ranges::find_if(m_views, [&view](const auto& candidate) { return candidate.get() == &view; });
    if (held == m_views.end()) {
        return nullptr;
    }

    view.m_removing = true;
    const std::weak_ptr<void> alive = view.m_lifetime;

    if (view.m_accessibleId != 0 && !view.m_destroyedAnnounced) {
        view.announce(QAccessible::ObjectDestroyed);
    }
    if (alive.expired()) {
        return nullptr;
    }

    held = std::ranges::find_if(m_views, [&view](const auto& candidate) { return candidate.get() == &view; });
    if (held == m_views.end()) {
        view.m_removing = false;
        return nullptr;
    }

    std::unique_ptr<ViewObject> removed = std::move(*held);
    m_views.erase(held);
    removed->m_removing = false;
    removed->m_formerAccessibleParent = accessible();
    removed->m_root = nullptr;
    addDamage(removed->paintBounds());
    leftTheTree(*removed);
    // The hovered view object may be the removed one or part of it; it learns that the pointer left before it goes.
    if (m_hovered.get() != nullptr) {
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
        if (ViewObject* owner = m_focusOwner.get()) {
            owner->handleEvent(event);
        }
        break;
    case QEvent::InputMethod:
        if (ViewObject* owner = m_inputMethodOwner.get()) {
            owner->handleEvent(event);
        }
        break;
    default:
        break;
    }
}

void ViewRoot::inputMethodQuery(QInputMethodQueryEvent& query) {
    if (ViewObject* owner = m_inputMethodOwner.get()) {
        owner->inputMethodQuery(query);
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

// The gesture whose owner is the view, or inside it, or held by it has no view to go to from now on.
// Descendants also give up focus and input method ownership.
void ViewRoot::leftTheTree(const ViewObject& view) {
    bool inside = m_pointerOwner.holder == &view;
    for (const ViewObject* owner = m_pointerOwner.view.get(); owner != nullptr && !inside; owner = owner->m_parent) {
        inside = owner == &view;
    }
    if (inside) {
        m_pointerOwner.view = {};
    }
    for (const ViewObject* owner = m_focusOwner.get(); owner != nullptr; owner = owner->m_parent) {
        if (owner == &view) {
            m_focusOwner = {};
            break;
        }
    }
    for (const ViewObject* owner = m_inputMethodOwner.get(); owner != nullptr; owner = owner->m_parent) {
        if (owner == &view) {
            m_inputMethodOwner = {};
            break;
        }
    }
}

void ViewRoot::dispatchPointer(const QSinglePointEvent& event) {
    const QEvent::Type type = event.type();
    const bool press =
        type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick || type == QEvent::TabletPress;
    const bool release = type == QEvent::MouseButtonRelease || type == QEvent::TabletRelease;
    const bool held = event.buttons() != Qt::NoButton;
    const bool firstButton = press && event.buttons() == Qt::MouseButtons(event.button());
    m_pointer = event.position();
    if ((!held && !release) || firstButton) {
        m_pointerOwner = {}; // nothing is held, or the first button goes down: an earlier gesture lost its release
    }
    updateHover(true);
    // Hover callbacks can change the tree, so the target is found only now. A wheel turns whatever is under the
    // pointer.
    if (type == QEvent::Wheel) {
        if (ViewObject* target = viewAt(m_pointer)) {
            target->handleEvent(event);
        }
        return;
    }
    // The owner of a gesture takes all of it, and nobody else when it is gone.
    const bool inGesture = m_pointerOwner.holder != nullptr;
    const Hit target =
        inGesture ? Hit{.holder = m_pointerOwner.holder, .view = m_pointerOwner.view.get()} : hitAt(m_pointer);
    if (press && !inGesture) {
        if (target.view != nullptr) {
            m_pointerOwner = {.holder = target.holder, .view = Ref(target.view)};
        }
    } else if (release && !held) {
        m_pointerOwner = {};
    }
    if (target.view != nullptr) {
        target.view->handleEvent(event);
    }
}

// m_hovered is updated before each callback runs. A callback can remove views, including the one it was called on or
// the one that is to be entered, and a holder can destroy a descendant that was hovered, which then gets no callback.
// The view to enter is found again after every callback.
void ViewRoot::updateHover(bool pointerInside) {
    for (;;) {
        ViewObject* target = pointerInside ? viewAt(m_pointer) : nullptr;
        ViewObject* hovered = m_hovered.get();
        if (target == hovered) {
            return;
        }
        if (hovered != nullptr) {
            m_hovered = {};
            hovered->hoverChanged(false);
            continue;
        }
        m_hovered = Ref(target);
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
    // come back with it. It paints within its declared bounds only, which is all that adding, moving and removing it
    // damages.
    for (const auto& view : m_views) {
        painter.save();
        painter.setClipRect(view->paintBounds().toAlignedRect(), Qt::IntersectClip);
        view->paint(painter);
        painter.restore();
    }
}

QRect ViewRoot::toScreen(QRect area) const {
    return m_surfaceToScreen ? QRect(m_surfaceToScreen(area.topLeft()), area.size()) : area;
}

} // namespace ariadshot::ui
