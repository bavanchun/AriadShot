// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/text/CanvasTextControl.h"

#include "render/text/FontSet.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QSinglePointEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace ariadshot::ui {

namespace {
// Inset is 4 pt on all sides (macshot/macshot/UI/Tools/TextEditingController.swift:295@b4d4f3a).
constexpr qreal kInset = 4.0;
} // namespace

CanvasTextControl::CanvasTextControl() : m_document(std::make_unique<QTextDocument>()), m_cursor(m_document.get()) {
    m_document->setDocumentMargin(0);
}

CanvasTextControl::~CanvasTextControl() = default;

QString CanvasTextControl::accessibleName() const {
    if (m_document && !m_document->toPlainText().isEmpty()) {
        return m_document->toPlainText();
    }
    return QStringLiteral("Text Editor");
}

QRectF CanvasTextControl::paintBounds() const { return geometry().adjusted(-kInset, -kInset, kInset, kInset); }

void CanvasTextControl::openNew(QPointF point, qreal fontSize) {
    m_fontSize = std::clamp(fontSize, 8.0, 200.0);
    const qreal height = std::max(28.0, m_fontSize + 12.0);
    setGeometry(QRectF{point.x(), point.y(), 200.0, height});
    m_editing = true;
    m_preeditString.clear();
    m_preeditCursorPos = 0;
    m_document->clear();
    m_document->setDefaultFont(currentFont());
    m_cursor = QTextCursor(m_document.get());
    updateLayout();
    m_lastDamage = paintBounds();
    update(m_lastDamage);
}

void CanvasTextControl::openReEdit(QRectF textDrawRect, const QString& text, qreal fontSize) {
    m_fontSize = std::clamp(fontSize, 8.0, 200.0);
    setGeometry(textDrawRect);
    m_editing = true;
    m_preeditString.clear();
    m_preeditCursorPos = 0;
    m_document->clear();
    m_document->setDefaultFont(currentFont());
    m_document->setPlainText(text);
    m_cursor = QTextCursor(m_document.get());
    m_cursor.movePosition(QTextCursor::End);
    updateLayout();
    m_lastDamage = paintBounds();
    update(m_lastDamage);
}

void CanvasTextControl::commit() {
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_preeditString.clear();
    m_preeditCursorPos = 0;

    const QString plainText = m_document ? m_document->toPlainText() : QString();
    if (plainText.isEmpty()) {
        if (m_onCommitted) {
            m_onCommitted(QString(), QRectF());
        }
    } else {
        updateLayout();
        const qreal textWidth = m_document->idealWidth();
        const qreal minH = std::max(28.0, m_fontSize + 12.0);
        const qreal imgHeight = std::max(minH, std::ceil(m_document->size().height()) + 2.0 * kInset);
        const qreal fittedWidth = std::ceil(textWidth) + 2.0 * kInset;
        const qreal imgWidth = std::max(fittedWidth, 20.0);
        const QRectF finalRect(geometry().x(), geometry().y(), imgWidth, imgHeight);
        if (m_onCommitted) {
            m_onCommitted(plainText, finalRect);
        }
    }
    if (m_document) {
        m_document->clearUndoRedoStacks();
    }
    m_lastDamage = paintBounds();
    update(m_lastDamage);
}

void CanvasTextControl::cancel() {
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_preeditString.clear();
    m_preeditCursorPos = 0;
    if (m_onCancelled) {
        m_onCancelled();
    }
    if (m_document) {
        m_document->clearUndoRedoStacks();
    }
    m_lastDamage = paintBounds();
    update(m_lastDamage);
}

QString CanvasTextControl::text() const { return m_document ? m_document->toPlainText() : QString(); }

void CanvasTextControl::setFontFamily(const QString& family) {
    m_fontFamily = family;
    if (m_document) {
        m_document->setDefaultFont(currentFont());
        updateLayout();
    }
}

void CanvasTextControl::setFontSize(qreal size) {
    m_fontSize = std::clamp(size, 8.0, 200.0);
    if (m_document) {
        m_document->setDefaultFont(currentFont());
        updateLayout();
    }
}

void CanvasTextControl::setBold(bool bold) {
    m_bold = bold;
    if (m_document) {
        m_document->setDefaultFont(currentFont());
        updateLayout();
    }
}

void CanvasTextControl::setItalic(bool italic) {
    m_italic = italic;
    if (m_document) {
        m_document->setDefaultFont(currentFont());
        updateLayout();
    }
}

void CanvasTextControl::setUnderline(bool underline) {
    m_underline = underline;
    if (m_document) {
        m_document->setDefaultFont(currentFont());
        updateLayout();
    }
}

void CanvasTextControl::setStrikethrough(bool strikethrough) {
    m_strikethrough = strikethrough;
    if (m_document) {
        m_document->setDefaultFont(currentFont());
        updateLayout();
    }
}

void CanvasTextControl::setTextColor(QColor color) {
    m_textColor = color;
    m_lastDamage = paintBounds();
    update(m_lastDamage);
}

void CanvasTextControl::setAlignment(Qt::AlignmentFlag alignment) {
    m_alignment = alignment;
    if (m_document) {
        m_document->setDefaultTextOption(QTextOption(alignment));
        updateLayout();
    }
}

QFont CanvasTextControl::currentFont() const {
    if (m_fontSet != nullptr) {
        return m_fontSet->textFont(m_fontFamily, m_fontSize, m_bold, m_italic);
    }
    QFont font;
    if (!m_fontFamily.isEmpty()) {
        font.setFamily(m_fontFamily);
    }
    font.setPointSizeF(m_fontSize);
    font.setBold(m_bold);
    font.setItalic(m_italic);
    font.setUnderline(m_underline);
    font.setStrikeOut(m_strikethrough);
    return font;
}

QRectF CanvasTextControl::cursorSurfaceRect() const {
    const QPointF basePos = geometry().topLeft() + QPointF(kInset, kInset);
    qreal x = 0.0;
    qreal y = 0.0;
    qreal h = m_fontSize + 4.0;

    if (m_document) {
        const QTextBlock block = m_cursor.block();
        if (block.isValid()) {
            if (const QTextLayout* const layout = block.layout()) {
                const int relPos = m_cursor.position() - block.position();
                const QTextLine line = layout->lineForTextPosition(relPos);
                if (line.isValid()) {
                    x = line.cursorToX(relPos);
                    y = line.y();
                    h = line.height();
                }
            }
        }
    }

    if (!m_preeditString.isEmpty()) {
        const QFontMetricsF fm(currentFont());
        x += fm.horizontalAdvance(m_preeditString.left(m_preeditCursorPos));
    }

    return {basePos.x() + x, basePos.y() + y, 2.0, h};
}

void CanvasTextControl::updateLayout() {
    if (!m_document) {
        return;
    }
    const qreal drawWidth = std::max(1.0, geometry().width() - 2.0 * kInset);
    m_document->setTextWidth(drawWidth);
    (void)m_document->documentLayout()->documentSize();

    const qreal layoutHeight = m_document->toPlainText().isEmpty() ? 0.0 : m_document->size().height();
    const qreal minH = std::max(28.0, m_fontSize + 12.0);
    const qreal liveHeight = std::max(minH, layoutHeight + 2.0 * kInset);

    if (liveHeight != geometry().height()) {
        setGeometry(QRectF{geometry().x(), geometry().y(), geometry().width(), liveHeight});
    }
}

void CanvasTextControl::resizeToFit() { updateLayout(); }

void CanvasTextControl::paint(QPainter& painter) {
    if (!m_editing && (m_document == nullptr || m_document->toPlainText().isEmpty())) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_bgColor.has_value()) {
        painter.setBrush(*m_bgColor);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(paintBounds(), 4.0, 4.0);
    }

    if (m_outlineColor.has_value() && !m_glyphStrokeColor.has_value()) {
        const QPen outlinePen(*m_outlineColor, 2.0);
        painter.setPen(outlinePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(paintBounds(), 4.0, 4.0);
    }

    if (m_editing) {
        const QPen boxPen(QColor(100, 150, 240, 150), 1.0, Qt::DashLine);
        painter.setPen(boxPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(geometry());
    }

    const QPointF textOrigin = geometry().topLeft() + QPointF(kInset, kInset);
    painter.translate(textOrigin);

    const QRectF clipRect(0, 0, geometry().width() - 2.0 * kInset, geometry().height() - 2.0 * kInset);
    painter.setClipRect(clipRect);

    if (m_document) {
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette.setColor(QPalette::Text, m_textColor);
        ctx.cursorPosition = m_cursor.position();
        m_document->documentLayout()->draw(&painter, ctx);
    }

    if (!m_preeditString.isEmpty()) {
        qreal px = 0.0;
        qreal py = 0.0;
        const QTextBlock block = m_cursor.block();
        if (block.isValid()) {
            if (const QTextLayout* const layout = block.layout()) {
                const int relPos = m_cursor.position() - block.position();
                const QTextLine line = layout->lineForTextPosition(relPos);
                if (line.isValid()) {
                    px = line.cursorToX(relPos);
                    py = line.y();
                }
            }
        }
        painter.setFont(currentFont());
        painter.setPen(m_textColor);
        const QFontMetricsF fm(currentFont());
        const qreal preeditWidth = fm.horizontalAdvance(m_preeditString);
        const qreal baseline = py + fm.ascent();
        painter.drawText(QPointF{px, baseline}, m_preeditString);

        const QPen underlinePen(m_textColor, 1.0, Qt::DashLine);
        painter.setPen(underlinePen);
        painter.drawLine(QPointF{px, baseline + 2.0}, QPointF{px + preeditWidth, baseline + 2.0});
    }

    if (m_editing) {
        const QRectF cr = cursorSurfaceRect().translated(-textOrigin);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_textColor);
        painter.drawRect(cr);
    }

    painter.restore();
}

ViewObject* CanvasTextControl::hitTest(QPointF) {
    if (!m_editing) {
        return nullptr;
    }
    return this;
}

void CanvasTextControl::handleEvent(const QEvent& event) {
    if (event.type() == QEvent::InputMethod) {
        const auto& imEvent = static_cast<const QInputMethodEvent&>(event);
        handleInputMethod(imEvent);
    } else if (event.type() == QEvent::KeyPress) {
        const auto& keyEvent = static_cast<const QKeyEvent&>(event);
        handleKeyPress(keyEvent);
    } else if (event.type() == QEvent::MouseButtonPress) {
        const auto& mouseEvent = static_cast<const QSinglePointEvent&>(event);
        if (!geometry().contains(mouseEvent.position())) {
            commit();
            return;
        }
        const QPointF localPt = mouseEvent.position() - geometry().topLeft() - QPointF(kInset, kInset);
        if (m_document && m_document->documentLayout()) {
            const int pos = m_document->documentLayout()->hitTest(localPt, Qt::FuzzyHit);
            if (pos >= 0) {
                m_cursor.setPosition(pos);
            }
        }
        m_lastDamage = paintBounds();
        update(m_lastDamage);
    }
}

void CanvasTextControl::handleInputMethod(const QInputMethodEvent& event) {
    if (!m_editing) {
        return;
    }

    const int rStart = event.replacementStart();
    const int rLength = event.replacementLength();
    if (rLength > 0) {
        QTextCursor repCursor = m_cursor;
        const int base = m_cursor.position();
        repCursor.setPosition(std::max(0, base + rStart));
        repCursor.setPosition(std::min(m_document->characterCount() - 1, base + rStart + rLength),
                              QTextCursor::KeepAnchor);
        repCursor.removeSelectedText();
    }

    const QString& commit = event.commitString();
    if (!commit.isEmpty()) {
        m_cursor.beginEditBlock();
        m_cursor.insertText(commit);
        m_cursor.endEditBlock();
        m_preeditString.clear();
        m_preeditCursorPos = 0;
    }

    const QString& preedit = event.preeditString();
    m_preeditString = preedit;
    m_preeditCursorPos = preedit.length();
    for (const auto& attr : event.attributes()) {
        if (attr.type == QInputMethodEvent::Cursor) {
            m_preeditCursorPos = attr.start;
        }
    }

    updateLayout();
    m_lastDamage = paintBounds();
    update(m_lastDamage);
}

void CanvasTextControl::handleKeyPress(const QKeyEvent& event) {
    if (!m_editing) {
        return;
    }

    if (event.key() == Qt::Key_Escape) {
        cancel();
        return;
    }

    // Undo / Redo
    if ((event.modifiers() & Qt::ControlModifier) && !(event.modifiers() & Qt::ShiftModifier) &&
        event.key() == Qt::Key_Z) {
        if (m_document) {
            m_document->undo(&m_cursor);
            updateLayout();
            m_lastDamage = paintBounds();
            update(m_lastDamage);
        }
        return;
    }

    if (((event.modifiers() & Qt::ControlModifier) && (event.modifiers() & Qt::ShiftModifier) &&
         event.key() == Qt::Key_Z) ||
        ((event.modifiers() & Qt::ControlModifier) && event.key() == Qt::Key_Y)) {
        if (m_document) {
            m_document->redo(&m_cursor);
            updateLayout();
            m_lastDamage = paintBounds();
            update(m_lastDamage);
        }
        return;
    }

    if (event.key() == Qt::Key_Return || event.key() == Qt::Key_Enter) {
        m_cursor.beginEditBlock();
        m_cursor.insertText(QStringLiteral("\n"));
        m_cursor.endEditBlock();
        updateLayout();
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    }

    if (event.key() == Qt::Key_Backspace) {
        m_cursor.beginEditBlock();
        if (!m_cursor.hasSelection() && m_cursor.position() > 0) {
            m_cursor.deletePreviousChar();
        } else if (m_cursor.hasSelection()) {
            m_cursor.removeSelectedText();
        }
        m_cursor.endEditBlock();
        updateLayout();
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    }

    if (event.key() == Qt::Key_Delete) {
        m_cursor.beginEditBlock();
        if (!m_cursor.hasSelection()) {
            m_cursor.deleteChar();
        } else {
            m_cursor.removeSelectedText();
        }
        m_cursor.endEditBlock();
        updateLayout();
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    }

    const QTextCursor::MoveMode moveMode =
        (event.modifiers() & Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
    switch (event.key()) {
    case Qt::Key_Left:
        m_cursor.movePosition(QTextCursor::Left, moveMode);
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    case Qt::Key_Right:
        m_cursor.movePosition(QTextCursor::Right, moveMode);
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    case Qt::Key_Up:
        m_cursor.movePosition(QTextCursor::Up, moveMode);
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    case Qt::Key_Down:
        m_cursor.movePosition(QTextCursor::Down, moveMode);
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    case Qt::Key_Home:
        m_cursor.movePosition(QTextCursor::StartOfLine, moveMode);
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    case Qt::Key_End:
        m_cursor.movePosition(QTextCursor::EndOfLine, moveMode);
        m_lastDamage = paintBounds();
        update(m_lastDamage);
        return;
    default:
        break;
    }

    const QString text = event.text();
    if (!text.isEmpty() && text.at(0).isPrint()) {
        m_cursor.beginEditBlock();
        m_cursor.insertText(text);
        m_cursor.endEditBlock();
        updateLayout();
        m_lastDamage = paintBounds();
        update(m_lastDamage);
    }
}

void CanvasTextControl::inputMethodQuery(QInputMethodQueryEvent& query) {
    const Qt::InputMethodQueries queries = query.queries();
    if (queries & Qt::ImCursorRectangle) {
        query.setValue(Qt::ImCursorRectangle, cursorSurfaceRect());
    }
    if (queries & Qt::ImSurroundingText) {
        query.setValue(Qt::ImSurroundingText, m_document ? m_document->toPlainText() : QString());
    }
    if (queries & Qt::ImCursorPosition) {
        query.setValue(Qt::ImCursorPosition, m_cursor.position());
    }
    if (queries & Qt::ImAnchorPosition) {
        query.setValue(Qt::ImAnchorPosition, m_cursor.anchor());
    }
    if (queries & Qt::ImFont) {
        query.setValue(Qt::ImFont, currentFont());
    }
    if (queries & Qt::ImHints) {
        query.setValue(Qt::ImHints, static_cast<int>(Qt::ImhMultiLine));
    }
}

std::optional<Qt::CursorShape> CanvasTextControl::cursorAt(QPointF) const { return Qt::IBeamCursor; }

} // namespace ariadshot::ui
