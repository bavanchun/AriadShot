// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/ViewObject.h"

#include <QAccessible>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace ariadshot::ui {

class ViewRoot;

// Ports MacShot's in-surface menus (e.g. Save menu, tools menu).
// In-surface chrome menu: keyboard navigation, item triggering, outside click/Esc dismissal.
class ChromeMenu : public ViewObject {
  public:
    struct Item {
        QString text;
        QString shortcut;
        bool enabled = true;
        bool checked = false;
        bool isSeparator = false;
        std::function<void()> triggered;
    };

    enum class Edge : std::uint8_t {
        Top,
        Bottom,
        Left,
        Right,
    };

    explicit ChromeMenu(QString title = {});
    ~ChromeMenu() override;

    [[nodiscard]] QString accessibleName() const override {
        return m_title.isEmpty() ? QStringLiteral("Menu") : m_title;
    }
    [[nodiscard]] QAccessible::Role accessibleRole() const override { return QAccessible::PopupMenu; }

    void addAction(const QString& text, std::function<void()> callback = nullptr, const QString& shortcut = {});
    void addSeparator();
    void clear();

    [[nodiscard]] int itemCount() const { return static_cast<int>(m_items.size()); }
    [[nodiscard]] const Item& itemAt(int index) const { return m_items.at(static_cast<std::size_t>(index)); }
    [[nodiscard]] int hoveredIndex() const { return m_hoveredIndex; }

    void setAnchorView(const ViewObject* anchorView);
    [[nodiscard]] const ViewObject* anchorView() const { return m_anchorView; }

    void setAnchorPoint(QPointF point);
    [[nodiscard]] QPointF anchorPoint() const { return m_anchorPoint; }

    void open(const ViewObject* anchorView, Edge edge = Edge::Bottom);
    void open(QPointF anchorPoint, Edge edge = Edge::Bottom);
    void open(ViewRoot& root, const ViewObject* anchorView, Edge edge = Edge::Bottom);
    void open(ViewRoot& root, QPointF anchorPoint, Edge edge = Edge::Bottom);

    void followAnchor();
    void updatePlacement();

    void dismiss();
    [[nodiscard]] bool isOpen() const { return m_open; }

    void triggerItem(int index);

    ViewObject* hitTest(QPointF point) override;
    void handleEvent(const QEvent& event) override;
    void paint(QPainter& painter) override;

  private:
    [[nodiscard]] int itemIndexAt(QPointF pos) const;
    void selectNext();
    void selectPrevious();

    QString m_title;
    Edge m_edge = Edge::Bottom;
    const ViewObject* m_anchorView = nullptr;
    QPointF m_anchorPoint;
    std::vector<Item> m_items;
    int m_hoveredIndex = -1;
    bool m_open = false;
};

} // namespace ariadshot::ui
