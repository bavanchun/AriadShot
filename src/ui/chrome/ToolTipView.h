// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/ViewObject.h"

#include <QAccessible>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>

namespace ariadshot::ui {

class ViewRoot;

// Ports MacShot's tooltip presentation inside the chrome overlay.
// Anchors to a point or view, follows moving anchors, and dismisses on click or Esc.
class ToolTipView : public ViewObject {
  public:
    enum class Edge : std::uint8_t {
        Top,
        Bottom,
    };

    explicit ToolTipView(QString text = {});
    ~ToolTipView() override;

    [[nodiscard]] QString accessibleName() const override { return m_text; }
    [[nodiscard]] QAccessible::Role accessibleRole() const override { return QAccessible::ToolTip; }

    void setText(const QString& text);
    [[nodiscard]] QString text() const { return m_text; }

    void setAnchorView(const ViewObject* anchorView);
    [[nodiscard]] const ViewObject* anchorView() const { return m_anchorView; }

    void setAnchorPoint(QPointF point);
    [[nodiscard]] QPointF anchorPoint() const { return m_anchorPoint; }

    void open(const ViewObject* anchorView, const QString& text = {}, Edge edge = Edge::Bottom);
    void open(QPointF anchorPoint, const QString& text = {}, Edge edge = Edge::Bottom);
    void open(ViewRoot& root, const ViewObject* anchorView, const QString& text = {}, Edge edge = Edge::Bottom);
    void open(ViewRoot& root, QPointF anchorPoint, const QString& text = {}, Edge edge = Edge::Bottom);

    void followAnchor();
    void updatePlacement();

    void dismiss();
    [[nodiscard]] bool isOpen() const { return m_open; }

    ViewObject* hitTest(QPointF point) override;
    void handleEvent(const QEvent& event) override;
    void paint(QPainter& painter) override;

  private:
    QString m_text;
    Edge m_edge = Edge::Bottom;
    const ViewObject* m_anchorView = nullptr;
    QPointF m_anchorPoint;
    bool m_open = false;
};

} // namespace ariadshot::ui
