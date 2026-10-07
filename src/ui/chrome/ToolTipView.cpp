// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/chrome/ToolTipView.h"

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QSinglePointEvent>

namespace ariadshot::ui {

ToolTipView::ToolTipView(QString text) : m_text(std::move(text)) {}

ToolTipView::~ToolTipView() = default;

void ToolTipView::setText(const QString& text) {
    m_text = text;
    if (m_open) {
        updatePlacement();
    }
}

void ToolTipView::setAnchorView(const ViewObject* anchorView) {
    m_anchorView = anchorView;
    if (anchorView != nullptr) {
        m_anchorPoint = anchorView->geometry().center();
    }
}

void ToolTipView::setAnchorPoint(QPointF point) {
    m_anchorPoint = point;
    m_anchorView = nullptr;
}

void ToolTipView::open(const ViewObject* anchorView, const QString& text, Edge edge) {
    if (!text.isEmpty()) {
        m_text = text;
    }
    setAnchorView(anchorView);
    m_edge = edge;
    m_open = true;
    updatePlacement();
}

void ToolTipView::open(QPointF anchorPoint, const QString& text, Edge edge) {
    if (!text.isEmpty()) {
        m_text = text;
    }
    setAnchorPoint(anchorPoint);
    m_edge = edge;
    m_open = true;
    updatePlacement();
}

void ToolTipView::open(ViewRoot& /*root*/, const ViewObject* anchorView, const QString& text, Edge edge) {
    open(anchorView, text, edge);
}

void ToolTipView::open(ViewRoot& /*root*/, QPointF anchorPoint, const QString& text, Edge edge) {
    open(anchorPoint, text, edge);
}

void ToolTipView::followAnchor() {
    if (m_anchorView != nullptr) {
        m_anchorPoint = m_anchorView->geometry().center();
    }
    updatePlacement();
}

void ToolTipView::updatePlacement() {
    QFont font;
    font.setPointSizeF(11.0);
    const QFontMetricsF fm(font);
    const QRectF textBounds = fm.boundingRect(m_text);

    constexpr qreal kPadX = 8.0;
    constexpr qreal kPadY = 4.0;
    const qreal w = textBounds.width() + 2.0 * kPadX;
    const qreal h = textBounds.height() + 2.0 * kPadY;

    constexpr qreal kGap = 6.0;
    qreal x = 0;
    qreal y = 0;
    if (m_anchorView != nullptr) {
        const QRectF anchor = m_anchorView->geometry();
        x = anchor.center().x() - w / 2.0;
        y = (m_edge == Edge::Bottom) ? (anchor.bottom() + kGap) : (anchor.top() - h - kGap);
    } else {
        x = m_anchorPoint.x() - w / 2.0;
        y = (m_edge == Edge::Bottom) ? (m_anchorPoint.y() + kGap) : (m_anchorPoint.y() - h - kGap);
    }

    setGeometry(QRectF{x, y, w, h});
}

void ToolTipView::dismiss() {
    if (!m_open) {
        return;
    }
    m_open = false;
    update(paintBounds());
}

ViewObject* ToolTipView::hitTest(QPointF point) {
    if (!m_open) {
        return nullptr;
    }
    if (geometry().contains(point)) {
        return this;
    }
    // Any click outside dismisses the tooltip
    return this;
}

void ToolTipView::handleEvent(const QEvent& event) {
    if (!m_open) {
        return;
    }
    if (event.type() == QEvent::MouseButtonPress || event.type() == QEvent::KeyPress) {
        dismiss();
    }
}

void ToolTipView::paint(QPainter& painter) {
    if (!m_open || m_text.isEmpty()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(20, 20, 20, 230));
    painter.setPen(QColor(80, 80, 80, 180));
    painter.drawRoundedRect(geometry(), 4.0, 4.0);

    QFont font;
    font.setPointSizeF(11.0);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(geometry(), Qt::AlignCenter, m_text);
    painter.restore();
}

} // namespace ariadshot::ui
