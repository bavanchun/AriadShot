// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/ViewObject.h"

#include <QAccessible>
#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTextCursor>

#include <functional>
#include <memory>
#include <optional>

class QTextDocument;

namespace ariadshot::render {
class FontSet;
} // namespace ariadshot::render

namespace ariadshot::ui {

// Ports MacShot's text editing controller and scoped text view
// (macshot/macshot/UI/Tools/TextEditingController.swift@b4d4f3a,
//  macshot/macshot/UI/Tools/ScopedUndoTextView.swift@b4d4f3a).
// Built on public QTextDocument, QTextCursor and QTextLayout APIs;
// handles QInputMethodEvents inline and reports damage for edited region only.
class CanvasTextControl : public ViewObject {
  public:
    CanvasTextControl();
    ~CanvasTextControl() override;

    [[nodiscard]] QString accessibleName() const override;
    [[nodiscard]] QAccessible::Role accessibleRole() const override { return QAccessible::EditableText; }
    [[nodiscard]] QRectF paintBounds() const override;

    void openNew(QPointF point, qreal fontSize = 20.0);
    void openReEdit(QRectF textDrawRect, const QString& text, qreal fontSize = 20.0);

    void commit();
    void cancel();
    [[nodiscard]] bool isEditing() const { return m_editing; }

    [[nodiscard]] QString text() const;
    [[nodiscard]] QString preeditString() const { return m_preeditString; }
    [[nodiscard]] QTextDocument* document() const { return m_document.get(); }
    [[nodiscard]] QTextCursor cursor() const { return m_cursor; }

    void setFontSet(const render::FontSet* fontSet) { m_fontSet = fontSet; }
    void setFontFamily(const QString& family);
    [[nodiscard]] QString fontFamily() const { return m_fontFamily; }

    void setFontSize(qreal size);
    [[nodiscard]] qreal fontSize() const { return m_fontSize; }

    void setBold(bool bold);
    [[nodiscard]] bool isBold() const { return m_bold; }

    void setItalic(bool italic);
    [[nodiscard]] bool isItalic() const { return m_italic; }

    void setUnderline(bool underline);
    [[nodiscard]] bool isUnderline() const { return m_underline; }

    void setStrikethrough(bool strikethrough);
    [[nodiscard]] bool isStrikethrough() const { return m_strikethrough; }

    void setTextColor(QColor color);
    [[nodiscard]] QColor textColor() const { return m_textColor; }

    void setBackgroundColor(std::optional<QColor> color) { m_bgColor = color; }
    [[nodiscard]] std::optional<QColor> backgroundColor() const { return m_bgColor; }

    void setOutlineColor(std::optional<QColor> color) { m_outlineColor = color; }
    [[nodiscard]] std::optional<QColor> outlineColor() const { return m_outlineColor; }

    void setGlyphStrokeColor(std::optional<QColor> color) { m_glyphStrokeColor = color; }
    [[nodiscard]] std::optional<QColor> glyphStrokeColor() const { return m_glyphStrokeColor; }

    void setAlignment(Qt::AlignmentFlag alignment);
    [[nodiscard]] Qt::AlignmentFlag alignment() const { return m_alignment; }

    void setOnCommitted(std::function<void(const QString&, QRectF)> callback) { m_onCommitted = std::move(callback); }
    void setOnCancelled(std::function<void()> callback) { m_onCancelled = std::move(callback); }

    [[nodiscard]] QRectF lastReportedDamage() const { return m_lastDamage; }

    void paint(QPainter& painter) override;
    ViewObject* hitTest(QPointF point) override;
    void handleEvent(const QEvent& event) override;
    void inputMethodQuery(QInputMethodQueryEvent& query) override;
    [[nodiscard]] std::optional<Qt::CursorShape> cursorAt(QPointF point) const override;

  private:
    [[nodiscard]] QFont currentFont() const;
    [[nodiscard]] QRectF cursorSurfaceRect() const;
    void updateLayout();
    void resizeToFit();
    void handleInputMethod(const class QInputMethodEvent& event);
    void handleKeyPress(const class QKeyEvent& event);

    std::unique_ptr<QTextDocument> m_document;
    QTextCursor m_cursor;
    const render::FontSet* m_fontSet = nullptr;
    qreal m_fontSize = 20.0;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    bool m_strikethrough = false;
    QString m_fontFamily;
    Qt::AlignmentFlag m_alignment = Qt::AlignLeft;
    QColor m_textColor{255, 0, 0};
    std::optional<QColor> m_bgColor;
    std::optional<QColor> m_outlineColor;
    std::optional<QColor> m_glyphStrokeColor;

    QString m_preeditString;
    int m_preeditCursorPos = 0;
    bool m_editing = false;
    QRectF m_lastDamage;

    std::function<void(const QString&, QRectF)> m_onCommitted;
    std::function<void()> m_onCancelled;
};

} // namespace ariadshot::ui
