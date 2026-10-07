// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/chrome/ChromeMenu.h"

#include <QAccessibleActionInterface>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QSinglePointEvent>
#include <QStringList>

#include <memory>

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
    if (const auto* anchor = m_anchorView.get()) {
        m_anchorPoint = anchor->geometry().bottomLeft();
    }
}

void ChromeMenu::setAnchorPoint(QPointF point) {
    m_anchorPoint = point;
    m_anchorView = {};
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
    if (const auto* anchor = m_anchorView.get()) {
        m_anchorPoint = anchor->geometry().bottomLeft();
    } else if (m_anchorView.isExpired()) {
        m_anchorView = {};
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
    if (const auto* anchor = m_anchorView.get()) {
        const QRectF anchorGeo = anchor->geometry();
        switch (m_edge) {
        case Edge::Bottom:
            x = anchorGeo.left();
            y = anchorGeo.bottom() + kGap;
            break;
        case Edge::Top:
            x = anchorGeo.left();
            y = anchorGeo.top() - totalHeight - kGap;
            break;
        case Edge::Left:
            x = anchorGeo.left() - totalWidth - kGap;
            y = anchorGeo.top();
            break;
        case Edge::Right:
            x = anchorGeo.right() + kGap;
            y = anchorGeo.top();
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

void ChromeMenu::rootPointerPressed(QPointF point) {
    if (m_open && !geometry().contains(point)) {
        dismiss();
    }
}

ViewObject* ChromeMenu::hitTest(QPointF point) {
    if (!m_open || !geometry().contains(point)) {
        return nullptr;
    }
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

class ChromeMenuItemAccessible final : public QAccessibleInterface, public QAccessibleActionInterface {
  public:
    ChromeMenuItemAccessible(ChromeMenu& menu, int index) : m_menu(menu), m_index(index) {}

    bool isValid() const override { return !m_menu.isDying() && m_index >= 0 && m_index < m_menu.itemCount(); }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int /*x*/, int /*y*/) const override { return nullptr; }
    QAccessibleInterface* parent() const override { return m_menu.accessible(); }
    QAccessibleInterface* child(int /*index*/) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface* /*child*/) const override { return -1; }

    QString text(QAccessible::Text t) const override {
        if (!isValid()) {
            return {};
        }
        if (t == QAccessible::Name) {
            return m_menu.itemAt(m_index).text;
        }
        if (t == QAccessible::Accelerator) {
            return m_menu.itemAt(m_index).shortcut;
        }
        return {};
    }
    void setText(QAccessible::Text /*t*/, const QString& /*text*/) override {}

    QRect rect() const override {
        if (!isValid()) {
            return {};
        }
        const QRect menuRect = m_menu.screenRect();
        int y = menuRect.top() + 4;
        constexpr int kItemHeight = 26;
        constexpr int kSeparatorHeight = 8;
        for (int i = 0; i < m_index; ++i) {
            y += m_menu.itemAt(i).isSeparator ? kSeparatorHeight : kItemHeight;
        }
        const int h = m_menu.itemAt(m_index).isSeparator ? kSeparatorHeight : kItemHeight;
        return {menuRect.left() + 4, y, menuRect.width() - 8, h};
    }

    QAccessible::Role role() const override {
        if (!isValid()) {
            return QAccessible::NoRole;
        }
        return m_menu.itemAt(m_index).isSeparator ? QAccessible::Separator : QAccessible::MenuItem;
    }

    QAccessible::State state() const override {
        QAccessible::State s;
        if (!isValid()) {
            return s;
        }
        const auto& item = m_menu.itemAt(m_index);
        s.focusable = !item.isSeparator;
        s.disabled = !item.enabled;
        s.checked = item.checked;
        if (m_menu.hoveredIndex() == m_index) {
            s.focused = true;
        }
        return s;
    }

    void* interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::ActionInterface) {
            return static_cast<QAccessibleActionInterface*>(this);
        }
        return nullptr;
    }

    QStringList actionNames() const override {
        if (isValid() && !m_menu.itemAt(m_index).isSeparator && m_menu.itemAt(m_index).enabled) {
            return {pressAction()};
        }
        return {};
    }

    void doAction(const QString& actionName) override {
        if (isValid() && actionName == pressAction()) {
            m_menu.triggerItem(m_index);
        }
    }

    QStringList keyBindingsForAction(const QString& /*actionName*/) const override {
        if (isValid() && !m_menu.itemAt(m_index).shortcut.isEmpty()) {
            return {m_menu.itemAt(m_index).shortcut};
        }
        return {};
    }

  private:
    ChromeMenu& m_menu;
    int m_index;
};

class ChromeMenuAccessible final : public QAccessibleInterface {
  public:
    explicit ChromeMenuAccessible(ChromeMenu& menu) : m_menu(menu) {}

    ~ChromeMenuAccessible() override {
        for (const auto id : m_childIds) {
            if (id != 0) {
                QAccessible::deleteAccessibleInterface(id);
            }
        }
        m_childIds.clear();
    }

    bool isValid() const override { return !m_menu.isDying(); }
    QObject* object() const override { return nullptr; }

    QAccessibleInterface* childAt(int x, int y) const override {
        if (!isValid()) {
            return nullptr;
        }
        for (int i = 0; i < childCount(); ++i) {
            if (QAccessibleInterface* const c = child(i)) {
                if (c->rect().contains(x, y)) {
                    return c;
                }
            }
        }
        return nullptr;
    }

    QAccessibleInterface* parent() const override { return m_menu.accessibleParent(); }

    int childCount() const override {
        if (!isValid()) {
            return 0;
        }
        return m_menu.itemCount();
    }

    QAccessibleInterface* child(int index) const override {
        if (!isValid() || index < 0 || index >= m_menu.itemCount()) {
            return nullptr;
        }
        ensureChildrenCapacity();
        const auto idx = static_cast<std::size_t>(index);
        if (m_childIds[idx] == 0) {
            auto itemAccessible = std::make_unique<ChromeMenuItemAccessible>(m_menu, index);
            m_childIds[idx] = QAccessible::registerAccessibleInterface(itemAccessible.release());
        }
        return QAccessible::accessibleInterface(m_childIds[idx]);
    }

    int indexOfChild(const QAccessibleInterface* child) const override {
        if (!isValid() || child == nullptr) {
            return -1;
        }
        for (int i = 0; i < childCount(); ++i) {
            if (this->child(i) == child) {
                return i;
            }
        }
        return -1;
    }

    QString text(QAccessible::Text t) const override {
        if (t == QAccessible::Name && isValid()) {
            return m_menu.accessibleName();
        }
        return {};
    }
    void setText(QAccessible::Text /*t*/, const QString& /*text*/) override {}

    QRect rect() const override { return m_menu.screenRect(); }

    QAccessible::Role role() const override { return QAccessible::PopupMenu; }

    QAccessible::State state() const override { return {}; }

  private:
    void ensureChildrenCapacity() const {
        const auto count = static_cast<std::size_t>(m_menu.itemCount());
        if (m_childIds.size() != count) {
            if (m_childIds.size() > count) {
                for (std::size_t i = count; i < m_childIds.size(); ++i) {
                    if (m_childIds[i] != 0) {
                        QAccessible::deleteAccessibleInterface(m_childIds[i]);
                    }
                }
            }
            m_childIds.resize(count, 0);
        }
    }

    ChromeMenu& m_menu;
    mutable std::vector<QAccessible::Id> m_childIds;
};

QAccessibleInterface* ChromeMenu::accessible() {
    if (m_accessibleId == 0) {
        m_accessibleId =
            QAccessible::registerAccessibleInterface(std::make_unique<ChromeMenuAccessible>(*this).release());
    }
    return QAccessible::accessibleInterface(m_accessibleId);
}

} // namespace ariadshot::ui
