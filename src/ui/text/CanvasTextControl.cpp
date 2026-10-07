// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/text/CanvasTextControl.h"

#include "render/text/FontSet.h"

#include <QAbstractTextDocumentLayout>
#include <QAccessibleEditableTextInterface>
#include <QAccessibleTextInterface>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QPaintDevice>
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

class LayoutDevice72Dpi final : public QPaintDevice {
  public:
    QPaintEngine* paintEngine() const override { return nullptr; }

  protected:
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY:
            return 72;
        case PdmWidth:
        case PdmHeight:
            return 10000;
        default:
            return 0;
        }
    }
};
} // namespace

CanvasTextControl::CanvasTextControl()
    : m_layoutDevice(std::make_unique<LayoutDevice72Dpi>()), m_document(std::make_unique<QTextDocument>()),
      m_cursor(m_document.get()) {
    m_document->setDocumentMargin(0);
    m_document->documentLayout()->setPaintDevice(m_layoutDevice.get());
}

CanvasTextControl::~CanvasTextControl() = default;

QString CanvasTextControl::accessibleName() const { return QObject::tr("Canvas text editor"); }

QRectF CanvasTextControl::paintBounds() const { return geometry().adjusted(-kInset, -kInset, kInset, kInset); }

void CanvasTextControl::openNew(QPointF point, qreal fontSize) {
    m_fontSize = std::clamp(fontSize, 8.0, 200.0);
    const qreal height = std::max(28.0, m_fontSize + 12.0);
    setGeometry(QRectF{point.x(), point.y(), 200.0, height});
    m_editing = true;
    m_preeditString.clear();
    m_preeditCursorPos = 0;
    m_preeditFormats.clear();
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
    m_preeditFormats.clear();
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

    // If an active input method composition is pending, insert it before committing (Finding 5)
    if (!m_preeditString.isEmpty()) {
        m_cursor.beginEditBlock();
        m_cursor.insertText(m_preeditString);
        m_cursor.endEditBlock();
        m_preeditString.clear();
        m_preeditCursorPos = 0;
        m_preeditFormats.clear();
    }

    const QString plainText = m_document ? m_document->toPlainText() : QString();
    QRectF finalRect;
    if (!plainText.isEmpty()) {
        updateLayout();
        const qreal textWidth = m_document->idealWidth();
        const qreal minH = std::max(28.0, m_fontSize + 12.0);
        const qreal imgHeight = std::max(minH, std::ceil(m_document->size().height()) + 2.0 * kInset);
        const qreal fittedWidth = std::ceil(textWidth) + 2.0 * kInset;
        const qreal imgWidth = std::max(fittedWidth, 20.0);
        finalRect = QRectF(geometry().x(), geometry().y(), imgWidth, imgHeight);
    }

    if (m_document) {
        m_document->clearUndoRedoStacks();
    }
    m_lastDamage = paintBounds();
    update(m_lastDamage);

    // Invoke callback as the very last statement, moving it first so self-destruction is safe (Finding 2)
    auto cb = std::move(m_onCommitted);
    m_onCommitted = nullptr;
    if (cb) {
        cb(plainText, finalRect);
    }
}

void CanvasTextControl::cancel() {
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_preeditString.clear();
    m_preeditCursorPos = 0;
    m_preeditFormats.clear();

    if (m_document) {
        m_document->clearUndoRedoStacks();
    }
    m_lastDamage = paintBounds();
    update(m_lastDamage);

    // Invoke callback as the very last statement, moving it first so self-destruction is safe (Finding 2)
    auto cb = std::move(m_onCancelled);
    m_onCancelled = nullptr;
    if (cb) {
        cb();
    }
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
    QFont font;
    if (m_fontSet != nullptr) {
        font = m_fontSet->textFont(m_fontFamily, m_fontSize, m_bold, m_italic);
    } else {
        if (!m_fontFamily.isEmpty()) {
            font.setFamily(m_fontFamily);
        }
        font.setPointSizeF(m_fontSize);
        font.setBold(m_bold);
        font.setItalic(m_italic);
        font.setHintingPreference(QFont::PreferNoHinting);
        font.setStyleStrategy(QFont::NoSubpixelAntialias);
    }
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
                const int cursorTextPos = relPos + m_preeditCursorPos;
                const QTextLine line = layout->lineForTextPosition(cursorTextPos);
                if (line.isValid()) {
                    x = line.cursorToX(cursorTextPos);
                    y = line.y();
                    h = line.height();
                }
            }
        }
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

    const QTextBlock block = m_cursor.block();
    if (block.isValid()) {
        if (QTextLayout* const layout = block.layout()) {
            const int relPos = m_cursor.position() - block.position();
            layout->setPreeditArea(relPos, m_preeditString);
            if (!m_preeditString.isEmpty()) {
                QList<QTextLayout::FormatRange> ranges;
                for (auto r : m_preeditFormats) {
                    r.start += relPos;
                    ranges.append(r);
                }
                layout->setFormats(ranges);
            } else {
                layout->clearFormats();
            }
            layout->beginLayout();
            qreal y = 0.0;
            while (true) {
                QTextLine line = layout->createLine();
                if (!line.isValid()) {
                    break;
                }
                line.setLineWidth(drawWidth);
                line.setPosition(QPointF(0, y));
                y += line.height();
            }
            layout->endLayout();
        }
    }

    qreal layoutHeight =
        (m_document->toPlainText().isEmpty() && m_preeditString.isEmpty()) ? 0.0 : m_document->size().height();
    if (!m_preeditString.isEmpty() && block.isValid() && block.layout()) {
        layoutHeight = std::max(layoutHeight, block.layout()->boundingRect().bottom());
    }
    const qreal minH = std::max(28.0, m_fontSize + 12.0);
    const qreal liveHeight = std::max(minH, layoutHeight + 2.0 * kInset);

    if (liveHeight != geometry().height()) {
        setGeometry(QRectF{geometry().x(), geometry().y(), geometry().width(), liveHeight});
    }
}

void CanvasTextControl::resizeToFit() { updateLayout(); }

void CanvasTextControl::paint(QPainter& painter) {
    if (!m_editing) {
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

    const QPen boxPen(QColor(100, 150, 240, 150), 1.0, Qt::DashLine);
    painter.setPen(boxPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(geometry());

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

    const QRectF cr = cursorSurfaceRect().translated(-textOrigin);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_textColor);
    painter.drawRect(cr);

    painter.restore();
}

void CanvasTextControl::rootPointerPressed(QPointF point) {
    if (m_editing && !geometry().contains(point)) {
        commit();
    }
}

ViewObject* CanvasTextControl::hitTest(QPointF point) {
    if (!m_editing || !geometry().contains(point)) {
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
        m_preeditFormats.clear();
    }

    const QString& preedit = event.preeditString();
    m_preeditString = preedit;
    m_preeditCursorPos = preedit.length();
    m_preeditFormats.clear();

    for (const auto& attr : event.attributes()) {
        if (attr.type == QInputMethodEvent::Cursor) {
            m_preeditCursorPos = attr.start;
        } else if (attr.type == QInputMethodEvent::TextFormat) {
            QTextCharFormat fmt = qvariant_cast<QTextFormat>(attr.value).toCharFormat();
            QTextLayout::FormatRange range;
            range.start = attr.start;
            range.length = attr.length;
            range.format = fmt;
            m_preeditFormats.append(range);
        }
    }

    if (!m_preeditString.isEmpty() && m_preeditFormats.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setFontUnderline(true);
        fmt.setUnderlineStyle(QTextCharFormat::DashUnderline);
        QTextLayout::FormatRange range;
        range.start = 0;
        range.length = m_preeditString.length();
        range.format = fmt;
        m_preeditFormats.append(range);
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
    if (queries & Qt::ImEnabled) {
        query.setValue(Qt::ImEnabled, m_editing);
    }
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

class CanvasTextControlAccessible final : public QAccessibleInterface,
                                          public QAccessibleTextInterface,
                                          public QAccessibleEditableTextInterface {
  public:
    explicit CanvasTextControlAccessible(CanvasTextControl& control) : m_control(control) {}

    bool isValid() const override { return !m_control.isDying(); }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int /*x*/, int /*y*/) const override { return nullptr; }
    QAccessibleInterface* parent() const override { return m_control.accessibleParent(); }
    QAccessibleInterface* child(int /*index*/) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface* /*child*/) const override { return -1; }

    QString text(QAccessible::Text t) const override {
        if (!isValid()) {
            return {};
        }
        if (t == QAccessible::Name) {
            return m_control.accessibleName();
        }
        if (t == QAccessible::Value) {
            return m_control.text();
        }
        return {};
    }
    void setText(QAccessible::Text /*t*/, const QString& /*text*/) override {}

    QRect rect() const override { return m_control.screenRect(); }

    QAccessible::Role role() const override { return QAccessible::EditableText; }

    QAccessible::State state() const override {
        QAccessible::State s;
        if (!isValid()) {
            return s;
        }
        s.editable = true;
        s.focusable = true;
        s.focused = m_control.isEditing();
        return s;
    }

    void* interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::TextInterface) {
            return static_cast<QAccessibleTextInterface*>(this);
        }
        if (type == QAccessible::EditableTextInterface) {
            return static_cast<QAccessibleEditableTextInterface*>(this);
        }
        return nullptr;
    }

    // QAccessibleTextInterface
    void selection(int selectionIndex, int* startOffset, int* endOffset) const override {
        if (selectionIndex == 0 && m_control.m_cursor.hasSelection()) {
            if (startOffset != nullptr) {
                *startOffset = m_control.m_cursor.selectionStart();
            }
            if (endOffset != nullptr) {
                *endOffset = m_control.m_cursor.selectionEnd();
            }
        } else {
            if (startOffset != nullptr) {
                *startOffset = 0;
            }
            if (endOffset != nullptr) {
                *endOffset = 0;
            }
        }
    }

    int selectionCount() const override { return m_control.m_cursor.hasSelection() ? 1 : 0; }

    void addSelection(int startOffset, int endOffset) override {
        m_control.m_cursor.setPosition(startOffset);
        m_control.m_cursor.setPosition(endOffset, QTextCursor::KeepAnchor);
        m_control.m_lastDamage = m_control.paintBounds();
        m_control.update(m_control.m_lastDamage);
    }

    void removeSelection(int /*selectionIndex*/) override {
        m_control.m_cursor.clearSelection();
        m_control.m_lastDamage = m_control.paintBounds();
        m_control.update(m_control.m_lastDamage);
    }

    void setSelection(int /*selectionIndex*/, int startOffset, int endOffset) override {
        addSelection(startOffset, endOffset);
    }

    int cursorPosition() const override { return m_control.m_cursor.position(); }

    void setCursorPosition(int position) override {
        m_control.m_cursor.setPosition(position);
        m_control.m_lastDamage = m_control.paintBounds();
        m_control.update(m_control.m_lastDamage);
    }

    QString text(int startOffset, int endOffset) const override {
        return m_control.text().mid(startOffset, endOffset - startOffset);
    }

    int characterCount() const override { return m_control.text().length(); }

    QRect characterRect(int /*offset*/) const override { return m_control.cursorSurfaceRect().toRect(); }

    int offsetAtPoint(const QPoint& point) const override {
        if (m_control.m_document && m_control.m_document->documentLayout()) {
            const QPointF localPt = point - m_control.screenRect().topLeft() - QPointF(kInset, kInset);
            return m_control.m_document->documentLayout()->hitTest(localPt, Qt::FuzzyHit);
        }
        return -1;
    }

    void scrollToSubstring(int /*startIndex*/, int /*endIndex*/) override {}

    QString attributes(int /*offset*/, int* startOffset, int* endOffset) const override {
        if (startOffset != nullptr) {
            *startOffset = 0;
        }
        if (endOffset != nullptr) {
            *endOffset = characterCount();
        }
        return {};
    }

    // QAccessibleEditableTextInterface
    void deleteText(int startOffset, int endOffset) override {
        if (!m_control.m_document) {
            return;
        }
        QTextCursor c(m_control.m_document.get());
        c.setPosition(startOffset);
        c.setPosition(endOffset, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        m_control.updateLayout();
        m_control.m_lastDamage = m_control.paintBounds();
        m_control.update(m_control.m_lastDamage);
    }

    void insertText(int offset, const QString& text) override {
        if (!m_control.m_document) {
            return;
        }
        QTextCursor c(m_control.m_document.get());
        c.setPosition(offset);
        c.insertText(text);
        m_control.updateLayout();
        m_control.m_lastDamage = m_control.paintBounds();
        m_control.update(m_control.m_lastDamage);
    }

    void replaceText(int startOffset, int endOffset, const QString& text) override {
        if (!m_control.m_document) {
            return;
        }
        QTextCursor c(m_control.m_document.get());
        c.setPosition(startOffset);
        c.setPosition(endOffset, QTextCursor::KeepAnchor);
        c.insertText(text);
        m_control.updateLayout();
        m_control.m_lastDamage = m_control.paintBounds();
        m_control.update(m_control.m_lastDamage);
    }

  private:
    CanvasTextControl& m_control;
};

QAccessibleInterface* CanvasTextControl::accessible() {
    if (m_accessibleId == 0) {
        m_accessibleId =
            QAccessible::registerAccessibleInterface(std::make_unique<CanvasTextControlAccessible>(*this).release());
    }
    return QAccessible::accessibleInterface(m_accessibleId);
}

} // namespace ariadshot::ui
