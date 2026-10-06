// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewRoot.h"

#include "render/CanonicalImage.h"
#include "ui/ViewObject.h"

#include <QEnterEvent>
#include <QEvent>
#include <QPainter>

#include <algorithm>
#include <ranges>
#include <utility>

namespace ariadshot::ui {

ViewRoot::~ViewRoot() = default;

void ViewRoot::resize(QSize size, qreal scale) {
    m_surface = QRect(QPoint(), size);
    m_chrome = render::makeCanonicalImage(QSize(qRound(size.width() * scale), qRound(size.height() * scale)), scale);
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
    // The hovered view object may be the removed one or part of it; it learns that the pointer left before it goes.
    if (m_hovered != nullptr) {
        setHovered(viewAt(m_pointer));
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
    case QEvent::TabletRelease: {
        m_pointer = static_cast<const QSinglePointEvent&>(event).position();
        ViewObject* view = viewAt(m_pointer);
        setHovered(view);
        if (view != nullptr) {
            view->handleEvent(event);
        }
        break;
    }
    case QEvent::Enter:
        m_pointer = static_cast<const QEnterEvent&>(event).position();
        setHovered(viewAt(m_pointer));
        break;
    case QEvent::Leave:
        setHovered(nullptr);
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

// Damage is whole points, rounded outwards so that it never covers less than the change.
void ViewRoot::addDamage(QRectF area) { m_damage += area.toAlignedRect() & m_surface; }

ViewObject* ViewRoot::viewAt(QPointF point) const {
    for (const auto& view : std::views::reverse(m_views)) {
        if (ViewObject* hit = view->hitTest(point)) {
            return hit;
        }
    }
    return nullptr;
}

void ViewRoot::setHovered(ViewObject* view) {
    if (view == m_hovered) {
        return;
    }
    if (m_hovered != nullptr) {
        m_hovered->hoverChanged(false);
    }
    m_hovered = view;
    if (view != nullptr) {
        view->hoverChanged(true);
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
    for (const auto& view : m_views) {
        if (damage.intersects(view->geometry().toAlignedRect())) {
            painter.save();
            view->paint(painter);
            painter.restore();
        }
    }
}

} // namespace ariadshot::ui
