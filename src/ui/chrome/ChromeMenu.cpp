// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/chrome/ChromeMenu.h"

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QSinglePointEvent>

namespace ariadshot::ui {

ChromeMenu::ChromeMenu(QString title) : m_title(std::move(title)) {}

ChromeMenu::~ChromeMenu() = default;

void ChromeMenu::addAction(const QString& text, std::function<void()> callback, const QString& shortcut) {
    m_items.push_back(Item{
        .text = text,
        .shortcut = shortcut,
        .enabled = true,
        .checked = false,
        .isSeparator = false,
        .triggered = std::move(callback),
    });
    if (m_open) {
        updatePlacement();
    }
}

void ChromeMenu::addSeparator() {
    m_items.push_back(Item{
        .text = {},
        .shortcut = {},
        .enabled = false,
        .checked = false,
        .isSeparator = true,
        .triggered = nullptr,
    });
    if (m_open) {
        updatePlacement();
    }
}

void ChromeMenu::clear() {
    m_items.clear();
    m_hoveredIndex = -1;
    if (m_open) {
        updatePlacement();
    }
}

void ChromeMenu::setAnchorView(const ViewObject* anchorView) {
    m_anchorView = anchorView;
    if (anchorView != nullptr) {
        m_anchorPoint = anchorView->geometry().bottomLeft();
    }
}

void ChromeMenu::setAnchorPoint(QPointF point) {
    m_anchorPoint = point;
    m_anchorView = nullptr;
}

void ChromeMenu::open(const ViewObject* anchorView, Edge edge) {
    setAnchorView(anchorView);
    m_edge = edge;
    m_open = true;
    m_hoveredIndex = -1;
    updatePlacement();
}

void ChromeMenu::open(QPointF anchorPoint, Edge edge) {
    setAnchorPoint(anchorPoint);
    m_edge = edge;
    m_open = true;
    m_hoveredIndex = -1;
    updatePlacement();
}

void ChromeMenu::open(ViewRoot& /*root*/, const ViewObject* anchorView, Edge edge) { open(anchorView, edge); }

void ChromeMenu::open(ViewRoot& /*root*/, QPointF anchorPoint, Edge edge) { open(anchorPoint, edge); }

void ChromeMenu::followAnchor() {
    if (m_anchorView != nullptr) {
        m_anchorPoint = m_anchorView->geometry().bottomLeft();
    }
    updatePlacement();
}

void ChromeMenu::updatePlacement() {
    QFont font;
    font.setPointSizeF(12.0);
    const QFontMetricsF fm(font);

    qreal maxTextWidth = 80.0;
    qreal maxShortcutWidth = 0.0;
    qreal totalHeight = 8.0; // 4pt top and bottom padding

    constexpr qreal kItemHeight = 26.0;
    constexpr qreal kSeparatorHeight = 8.0;

    for (const auto& item : m_items) {
        if (item.isSeparator) {
            totalHeight += kSeparatorHeight;
        } else {
            totalHeight += kItemHeight;
            const qreal tw = fm.boundingRect(item.text).width();
            if (tw > maxTextWidth) {
                maxTextWidth = tw;
            }
            if (!item.shortcut.isEmpty()) {
                const qreal sw = fm.boundingRect(item.shortcut).width();
                if (sw > maxShortcutWidth) {
                    maxShortcutWidth = sw;
                }
            }
        }
    }

    qreal totalWidth = maxTextWidth + 36.0;
    if (maxShortcutWidth > 0.0) {
        totalWidth += maxShortcutWidth + 20.0;
    }
    if (totalWidth < 140.0) {
        totalWidth = 140.0;
    }

    constexpr qreal kGap = 2.0;
    qreal x = 0;
    qreal y = 0;
    if (m_anchorView != nullptr) {
        const QRectF anchor = m_anchorView->geometry();
        switch (m_edge) {
        case Edge::Bottom:
            x = anchor.left();
            y = anchor.bottom() + kGap;
            break;
        case Edge::Top:
            x = anchor.left();
            y = anchor.top() - totalHeight - kGap;
            break;
        case Edge::Left:
            x = anchor.left() - totalWidth - kGap;
            y = anchor.top();
            break;
        case Edge::Right:
            x = anchor.right() + kGap;
            y = anchor.top();
            break;
        }
    } else {
        x = m_anchorPoint.x();
        y = m_anchorPoint.y() + kGap;
    }

    setGeometry(QRectF{x, y, totalWidth, totalHeight});
}

void ChromeMenu::dismiss() {
    if (!m_open) {
        return;
    }
    m_open = false;
    m_hoveredIndex = -1;
    update(paintBounds());
}

void ChromeMenu::triggerItem(int index) {
    if (index >= 0 && static_cast<std::size_t>(index) < m_items.size()) {
        const auto& item = m_items[static_cast<std::size_t>(index)];
        if (item.enabled && !item.isSeparator) {
            auto cb = item.triggered;
            dismiss();
            if (cb) {
                cb();
            }
        }
    }
}

int ChromeMenu::itemIndexAt(QPointF pos) const {
    if (!geometry().contains(pos)) {
        return -1;
    }
    qreal y = geometry().top() + 4.0;
    constexpr qreal kItemHeight = 26.0;
    constexpr qreal kSeparatorHeight = 8.0;

    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const qreal h = m_items[i].isSeparator ? kSeparatorHeight : kItemHeight;
        if (pos.y() >= y && pos.y() < y + h) {
            return static_cast<int>(i);
        }
        y += h;
    }
    return -1;
}

void ChromeMenu::selectNext() {
    if (m_items.empty()) {
        return;
    }
    int next = m_hoveredIndex + 1;
    for (std::size_t count = 0; count < m_items.size(); ++count) {
        if (std::cmp_greater_equal(next, m_items.size())) {
            next = 0;
        }
        if (m_items[static_cast<std::size_t>(next)].enabled && !m_items[static_cast<std::size_t>(next)].isSeparator) {
            m_hoveredIndex = next;
            return;
        }
        ++next;
    }
}

void ChromeMenu::selectPrevious() {
    if (m_items.empty()) {
        return;
    }
    int prev = m_hoveredIndex - 1;
    for (std::size_t count = 0; count < m_items.size(); ++count) {
        if (prev < 0) {
            prev = static_cast<int>(m_items.size()) - 1;
        }
        if (m_items[static_cast<std::size_t>(prev)].enabled && !m_items[static_cast<std::size_t>(prev)].isSeparator) {
            m_hoveredIndex = prev;
            return;
        }
        --prev;
    }
}

ViewObject* ChromeMenu::hitTest(QPointF point) {
    if (!m_open) {
        return nullptr;
    }
    if (geometry().contains(point)) {
        return this;
    }
    // Outside click dismisses
    return this;
}

void ChromeMenu::handleEvent(const QEvent& event) {
    if (!m_open) {
        return;
    }
    if (event.type() == QEvent::MouseMove) {
        const auto& mouseEvent = static_cast<const QSinglePointEvent&>(event);
        const int idx = itemIndexAt(mouseEvent.position());
        if (idx != m_hoveredIndex) {
            m_hoveredIndex = idx;
            update(geometry());
        }
    } else if (event.type() == QEvent::MouseButtonPress) {
        const auto& mouseEvent = static_cast<const QSinglePointEvent&>(event);
        if (!geometry().contains(mouseEvent.position())) {
            dismiss();
            return;
        }
        const int idx = itemIndexAt(mouseEvent.position());
        if (idx >= 0) {
            triggerItem(idx);
        }
    } else if (event.type() == QEvent::KeyPress) {
        const auto& keyEvent = static_cast<const QKeyEvent&>(event);
        switch (keyEvent.key()) {
        case Qt::Key_Down:
            selectNext();
            update(geometry());
            break;
        case Qt::Key_Up:
            selectPrevious();
            update(geometry());
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (m_hoveredIndex >= 0) {
                triggerItem(m_hoveredIndex);
            }
            break;
        case Qt::Key_Escape:
            dismiss();
            break;
        default:
            break;
        }
    }
}

void ChromeMenu::paint(QPainter& painter) {
    if (!m_open) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Card background
    painter.setBrush(QColor(35, 35, 35, 240));
    painter.setPen(QColor(70, 70, 70, 180));
    painter.drawRoundedRect(geometry(), 6.0, 6.0);

    QFont font;
    font.setPointSizeF(12.0);
    painter.setFont(font);

    qreal y = geometry().top() + 4.0;
    constexpr qreal kItemHeight = 26.0;
    constexpr qreal kSeparatorHeight = 8.0;
    const qreal left = geometry().left();
    const qreal width = geometry().width();

    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];
        if (item.isSeparator) {
            painter.setPen(QColor(60, 60, 60, 200));
            painter.drawLine(QPointF{left + 8.0, y + kSeparatorHeight / 2.0},
                             QPointF{left + width - 8.0, y + kSeparatorHeight / 2.0});
            y += kSeparatorHeight;
        } else {
            const QRectF itemRect{left + 4.0, y, width - 8.0, kItemHeight};
            if (std::cmp_equal(i, m_hoveredIndex) && item.enabled) {
                painter.setBrush(QColor(70, 110, 180, 220));
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(itemRect, 4.0, 4.0);
            }
            painter.setPen(item.enabled ? Qt::white : QColor(140, 140, 140));
            painter.drawText(itemRect.adjusted(8.0, 0, -8.0, 0), Qt::AlignLeft | Qt::AlignVCenter, item.text);
            if (!item.shortcut.isEmpty()) {
                painter.setPen(QColor(160, 160, 160));
                painter.drawText(itemRect.adjusted(8.0, 0, -8.0, 0), Qt::AlignRight | Qt::AlignVCenter, item.shortcut);
            }
            y += kItemHeight;
        }
    }
    painter.restore();
}

} // namespace ariadshot::ui
