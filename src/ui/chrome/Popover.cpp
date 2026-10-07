// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/chrome/Popover.h"

#include <QKeyEvent>
#include <QPainter>
#include <QSinglePointEvent>

namespace ariadshot::ui {

Popover::Popover(QString name) : m_name(std::move(name)) {}

Popover::~Popover() = default;

void Popover::setContent(std::unique_ptr<ViewObject> content) {
    if (m_content) {
        removeChild(*m_content);
    }
    m_content = std::move(content);
    if (m_content) {
        (void)addChild(*m_content);
    }
}

void Popover::setPreferredSize(QSizeF size) {
    m_preferredSize = size;
    if (m_open) {
        updatePlacement();
    }
}

void Popover::setAnchorView(const ViewObject* anchorView) {
    m_anchorView = anchorView;
    if (anchorView != nullptr) {
        m_anchorRect = anchorView->geometry();
    }
}

void Popover::setAnchorRect(QRectF rect) {
    m_anchorRect = rect;
    m_anchorView = nullptr;
}

void Popover::open(const ViewObject* anchorView, Edge edge) {
    setAnchorView(anchorView);
    m_edge = edge;
    m_open = true;
    updatePlacement();
}

void Popover::open(QRectF anchorRect, Edge edge) {
    setAnchorRect(anchorRect);
    m_edge = edge;
    m_open = true;
    updatePlacement();
}

void Popover::open(ViewRoot& /*root*/, const ViewObject* anchorView, Edge edge) { open(anchorView, edge); }

void Popover::open(ViewRoot& /*root*/, QRectF anchorRect, Edge edge) { open(anchorRect, edge); }

void Popover::followAnchor() {
    if (const ViewObject* const anchor = m_anchorView.get()) {
        m_anchorRect = anchor->geometry();
        updatePlacement();
    } else if (m_anchorView.isExpired()) {
        m_anchorView = nullptr;
    }
}

void Popover::updatePlacement() {
    constexpr qreal kGap = 4.0; // macshot/macshot/UI/Overlay/OverlayView+Popovers.swift:39@b4d4f3a
    const QRectF anchor = m_anchorRect;
    const qreal w = m_preferredSize.width();
    const qreal h = m_preferredSize.height();

    qreal x = 0;
    qreal y = 0;
    switch (m_edge) {
    case Edge::Bottom:
        x = anchor.center().x() - w / 2.0;
        y = anchor.bottom() + kGap;
        break;
    case Edge::Top:
        x = anchor.center().x() - w / 2.0;
        y = anchor.top() - h - kGap;
        break;
    case Edge::Left:
        x = anchor.left() - w - kGap;
        y = anchor.center().y() - h / 2.0;
        break;
    case Edge::Right:
        x = anchor.right() + kGap;
        y = anchor.center().y() - h / 2.0;
        break;
    }

    setGeometry(QRectF{x, y, w, h});
    if (m_content) {
        m_content->setGeometry(QRectF{x + 4.0, y + 4.0, w - 8.0, h - 8.0});
    }
}

void Popover::dismiss() {
    if (!m_open) {
        return;
    }
    m_open = false;
    update(paintBounds());
    if (m_onDismissed) {
        m_onDismissed();
    }
}

ViewObject* Popover::hitTest(QPointF point) {
    if (!m_open) {
        return nullptr;
    }
    if (geometry().contains(point)) {
        if (m_content) {
            if (ViewObject* hit = m_content->hitTest(point)) {
                return hit;
            }
        }
        return this;
    }
    return nullptr;
}

void Popover::rootPointerPressed(QPointF point) {
    if (!m_open || !m_dismissOnOutsideClick) {
        return;
    }
    if (!geometry().contains(point)) {
        dismiss();
    }
}

void Popover::handleEvent(const QEvent& event) {
    if (!m_open) {
        return;
    }
    if (event.type() == QEvent::KeyPress) {
        const auto& keyEvent = static_cast<const QKeyEvent&>(event);
        if (m_dismissOnEscape && keyEvent.key() == Qt::Key_Escape) {
            dismiss();
            return;
        }
    }
}

void Popover::paint(QPainter& painter) {
    if (!m_open) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.setPen(QColor(70, 70, 70, 180));
    painter.drawRoundedRect(geometry(), 8.0, 8.0);
    painter.restore();

    if (m_content) {
        m_content->paint(painter);
    }
}

} // namespace ariadshot::ui
