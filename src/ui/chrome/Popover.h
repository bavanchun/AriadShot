// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/ViewObject.h"

#include <QAccessible>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

namespace ariadshot::ui {

class ViewRoot;

// Ports MacShot's popover container (macshot/macshot/UI/Popover/PopoverHelper.swift@b4d4f3a).
// A chrome view object drawn inside the surface; anchors to a view or a point/rect, follows moving
// anchors, and dismisses on outside click or Esc.
class Popover : public ViewObject {
  public:
    enum class Edge : std::uint8_t {
        Top,
        Bottom,
        Left,
        Right,
    };

    explicit Popover(QString name = {});
    ~Popover() override;

    [[nodiscard]] QString accessibleName() const override {
        return m_name.isEmpty() ? QStringLiteral("Popover") : m_name;
    }
    [[nodiscard]] QAccessible::Role accessibleRole() const override { return QAccessible::LayeredPane; }

    void setContent(std::unique_ptr<ViewObject> content);
    [[nodiscard]] ViewObject* content() const { return m_content.get(); }

    void setPreferredSize(QSizeF size);
    [[nodiscard]] QSizeF preferredSize() const { return m_preferredSize; }

    void setEdge(Edge edge) { m_edge = edge; }
    [[nodiscard]] Edge edge() const { return m_edge; }

    void setAnchorView(const ViewObject* anchorView);
    [[nodiscard]] const ViewObject* anchorView() const { return m_anchorView; }

    void setAnchorRect(QRectF rect);
    [[nodiscard]] QRectF anchorRect() const { return m_anchorRect; }

    void setDismissOnOutsideClick(bool dismiss) { m_dismissOnOutsideClick = dismiss; }
    [[nodiscard]] bool dismissOnOutsideClick() const { return m_dismissOnOutsideClick; }

    void setDismissOnEscape(bool dismiss) { m_dismissOnEscape = dismiss; }
    [[nodiscard]] bool dismissOnEscape() const { return m_dismissOnEscape; }

    void setOnDismissed(std::function<void()> callback) { m_onDismissed = std::move(callback); }

    // Opens the popover, positioned relative to anchor.
    void open(const ViewObject* anchorView, Edge edge = Edge::Bottom);
    void open(QRectF anchorRect, Edge edge = Edge::Bottom);
    void open(ViewRoot& root, const ViewObject* anchorView, Edge edge = Edge::Bottom);
    void open(ViewRoot& root, QRectF anchorRect, Edge edge = Edge::Bottom);

    void followAnchor();
    void updatePlacement();

    void dismiss();
    [[nodiscard]] bool isOpen() const { return m_open; }

    ViewObject* hitTest(QPointF point) override;
    void handleEvent(const QEvent& event) override;
    void paint(QPainter& painter) override;

  private:
    QString m_name;
    QSizeF m_preferredSize{200, 150};
    Edge m_edge = Edge::Bottom;
    const ViewObject* m_anchorView = nullptr;
    QRectF m_anchorRect;
    std::unique_ptr<ViewObject> m_content;
    std::function<void()> m_onDismissed;
    bool m_open = false;
    bool m_dismissOnOutsideClick = true;
    bool m_dismissOnEscape = true;
};

} // namespace ariadshot::ui
