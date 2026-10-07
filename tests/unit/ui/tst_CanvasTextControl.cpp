// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewRoot.h"
#include "ui/text/CanvasTextControl.h"

#include <QAccessible>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTest>
#include <QTextDocument>

#include <memory>
#include <vector>

namespace ariadshot::ui {
namespace {

void sendInputMethod(ViewRoot& root, CanvasTextControl& control, const QString& commit, const QString& preedit,
                     int replacementStart = 0, int replacementLength = 0) {
    QList<QInputMethodEvent::Attribute> attributes;
    if (!preedit.isEmpty()) {
        attributes.append(QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, 0, preedit.length(), {}));
    }
    const QInputMethodEvent event(preedit, attributes);
    // If commit is non-empty or replacement is specified, create full event
    QInputMethodEvent imEvent(preedit, attributes);
    imEvent.setCommitString(commit, replacementStart, replacementLength);
    root.setInputMethodOwner(&control);
    root.dispatch(imEvent);
}

void keyPress(ViewRoot& root, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = {}) {
    const QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    root.dispatch(event);
}

void mousePress(ViewRoot& root, QPointF position) {
    const QMouseEvent event(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
    root.dispatch(event);
}

} // namespace

class CanvasTextControlTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void preeditShownInlineAndNotCommitted();
    void commitReplacesPreedit();
    void vietnameseTelexBambooCompositionSequence();
    void inputMethodQueryReturnsCaretRectInSurfaceCoordinates();
    void inputMethodQuerySurroundingTextAndCursorPosition();
    void enterInsertsNewlineAndDoesNotCommit();
    void shiftEnterInsertsNewlineAndDoesNotCommit();
    void clickOutsideCommitsAndEscapeCancels();
    void frameAndLiveResizeGeometry();
    void scopedTypingUndo();
    void accessibleNameAndRole();
    void damageReportedForEditedRegionOnly();
};

// macshot/macshot/UI/Tools/TextEditingController.swift:256-268@b4d4f3a
// arch §3.1.5 step 4
void CanvasTextControlTest::preeditShownInlineAndNotCommitted() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setInputMethodOwner(controlPtr);

    QVERIFY(controlPtr->isEditing());
    QVERIFY(controlPtr->text().isEmpty());

    // Send preedit "chao"
    sendInputMethod(root, *controlPtr, QString(), QStringLiteral("chao"));

    // Preedit is visible inline, but not committed to document text
    QCOMPARE(controlPtr->preeditString(), QStringLiteral("chao"));
    QVERIFY(controlPtr->text().isEmpty());
    QCOMPARE(controlPtr->document()->toPlainText(), QString());
}

// macshot/macshot/UI/Tools/TextEditingController.swift:280-320@b4d4f3a
// arch §3.1.5 step 4
void CanvasTextControlTest::commitReplacesPreedit() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setInputMethodOwner(controlPtr);

    // Initial preedit
    sendInputMethod(root, *controlPtr, QString(), QStringLiteral("chao"));
    QCOMPARE(controlPtr->preeditString(), QStringLiteral("chao"));

    // Commit replaces preedit
    sendInputMethod(root, *controlPtr, QStringLiteral("chào"), QString());
    QVERIFY(controlPtr->preeditString().isEmpty());
    QCOMPARE(controlPtr->text(), QStringLiteral("chào"));
    QCOMPARE(controlPtr->document()->toPlainText(), QStringLiteral("chào"));
}

// arch §3.1.5 step 4 (Vietnamese Telex inline preedit and commit via QT_IM_MODULE=fcitx / text-input-v3)
// fcitx5-bamboo sends preedit updates for composition ('a' -> 'â', 'u' -> 'ư')
void CanvasTextControlTest::vietnameseTelexBambooCompositionSequence() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{50, 50}, 20.0);
    root.setInputMethodOwner(controlPtr);

    // 1. Typing 'a' in Telex: fcitx5-bamboo sends preedit "a"
    sendInputMethod(root, *controlPtr, QString(), QStringLiteral("a"));
    QCOMPARE(controlPtr->preeditString(), QStringLiteral("a"));
    QCOMPARE(controlPtr->text(), QString());

    // 2. Typing next 'a': bamboo transforms to "â" in preedit
    sendInputMethod(root, *controlPtr, QString(), QStringLiteral("â"));
    QCOMPARE(controlPtr->preeditString(), QStringLiteral("â"));
    QCOMPARE(controlPtr->text(), QString());

    // 3. Bamboo commits "â"
    sendInputMethod(root, *controlPtr, QStringLiteral("â"), QString());
    QVERIFY(controlPtr->preeditString().isEmpty());
    QCOMPARE(controlPtr->text(), QStringLiteral("â"));

    // 4. Typing 'u' then 'w' to produce 'ư'
    sendInputMethod(root, *controlPtr, QString(), QStringLiteral("u"));
    QCOMPARE(controlPtr->preeditString(), QStringLiteral("u"));
    QCOMPARE(controlPtr->text(), QStringLiteral("â"));

    // Bamboo replaces preedit and commits "ư"
    sendInputMethod(root, *controlPtr, QStringLiteral("ư"), QString());
    QVERIFY(controlPtr->preeditString().isEmpty());
    QCOMPARE(controlPtr->text(), QStringLiteral("âư"));
}

// arch §3.1.5 step 4 (CJK candidate window within 50 px of caret)
// inputMethodQuery(Qt::ImCursorRectangle) returns caret rect in surface coordinates
void CanvasTextControlTest::inputMethodQueryReturnsCaretRectInSurfaceCoordinates() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    const QPointF origin{150, 200};
    controlPtr->openNew(origin, 20.0);
    root.setInputMethodOwner(controlPtr);

    QInputMethodQueryEvent query(Qt::ImCursorRectangle);
    controlPtr->inputMethodQuery(query);
    const QRectF initialCaretRect = query.value(Qt::ImCursorRectangle).toRectF();

    // Caret must be in surface coordinates: origin is at (150, 200), inset is 4pt
    // macshot/macshot/UI/Tools/TextEditingController.swift:295@b4d4f3a
    QVERIFY(initialCaretRect.x() >= origin.x());
    QVERIFY(initialCaretRect.y() >= origin.y());
    QVERIFY(controlPtr->geometry().contains(initialCaretRect.topLeft()));

    // When text is typed, caret moves to the right
    sendInputMethod(root, *controlPtr, QStringLiteral("Testing"), QString());
    QInputMethodQueryEvent query2(Qt::ImCursorRectangle);
    controlPtr->inputMethodQuery(query2);
    const QRectF advancedCaretRect = query2.value(Qt::ImCursorRectangle).toRectF();

    QVERIFY(advancedCaretRect.x() > initialCaretRect.x());
    QVERIFY(controlPtr->geometry().contains(advancedCaretRect.topLeft()));
}

// spec 06 §3.2
void CanvasTextControlTest::inputMethodQuerySurroundingTextAndCursorPosition() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setInputMethodOwner(controlPtr);

    sendInputMethod(root, *controlPtr, QStringLiteral("Hello World"), QString());

    QInputMethodQueryEvent query(Qt::ImSurroundingText | Qt::ImCursorPosition | Qt::ImAnchorPosition);
    controlPtr->inputMethodQuery(query);

    QCOMPARE(query.value(Qt::ImSurroundingText).toString(), QStringLiteral("Hello World"));
    QCOMPARE(query.value(Qt::ImCursorPosition).toInt(), 11);
    QCOMPARE(query.value(Qt::ImAnchorPosition).toInt(), 11);
}

// macshot/macshot/UI/Overlay/OverlayView.swift:10256-10258@b4d4f3a
// Enter inserts newline; no key combination commits (spec 06 §3.2)
void CanvasTextControlTest::enterInsertsNewlineAndDoesNotCommit() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setFocusOwner(controlPtr);

    sendInputMethod(root, *controlPtr, QStringLiteral("Line1"), QString());
    QCOMPARE(controlPtr->text(), QStringLiteral("Line1"));

    // Press Return
    keyPress(root, Qt::Key_Return);
    QVERIFY(controlPtr->isEditing());
    QCOMPARE(controlPtr->text(), QStringLiteral("Line1\n"));

    sendInputMethod(root, *controlPtr, QStringLiteral("Line2"), QString());
    QCOMPARE(controlPtr->text(), QStringLiteral("Line1\nLine2"));
    QVERIFY(controlPtr->isEditing());
}

// macshot/macshot/UI/Overlay/OverlayView.swift:10256-10258@b4d4f3a
void CanvasTextControlTest::shiftEnterInsertsNewlineAndDoesNotCommit() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setFocusOwner(controlPtr);

    sendInputMethod(root, *controlPtr, QStringLiteral("First"), QString());
    keyPress(root, Qt::Key_Return, Qt::ShiftModifier);
    QVERIFY(controlPtr->isEditing());
    QCOMPARE(controlPtr->text(), QStringLiteral("First\n"));
}

// macshot/macshot/UI/Overlay/OverlayView.swift:8873-8880,10261@b4d4f3a
// spec 06 §3.2: click outside commits; Esc cancels
void CanvasTextControlTest::clickOutsideCommitsAndEscapeCancels() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setFocusOwner(controlPtr);

    QString committedText;
    QRectF committedRect;
    controlPtr->setOnCommitted([&](const QString& t, QRectF r) {
        committedText = t;
        committedRect = r;
    });

    sendInputMethod(root, *controlPtr, QStringLiteral("Committed content"), QString());

    // Click outside: point (500, 500) is far outside (100, 100, 200, 32)
    mousePress(root, QPointF{500, 500});
    QVERIFY(!controlPtr->isEditing());
    QCOMPARE(committedText, QStringLiteral("Committed content"));
    QVERIFY(!committedRect.isEmpty());

    // Test Esc cancels:
    bool cancelled = false;
    controlPtr->setOnCancelled([&]() { cancelled = true; });
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setFocusOwner(controlPtr);
    sendInputMethod(root, *controlPtr, QStringLiteral("To be cancelled"), QString());
    QVERIFY(controlPtr->isEditing());

    keyPress(root, Qt::Key_Escape);
    QVERIFY(!controlPtr->isEditing());
    QVERIFY(cancelled);
}

// macshot/macshot/UI/Tools/TextEditingController.swift:266,295,355-425,449-466@b4d4f3a
void CanvasTextControlTest::frameAndLiveResizeGeometry() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));

    // Initial frame: 200 x max(28, fontSize + 12)
    // For fontSize = 20: height = max(28, 32) = 32
    controlPtr->openNew(QPointF{50, 50}, 20.0);
    const QRectF initialGeo = controlPtr->geometry();
    QCOMPARE(initialGeo.width(), 200.0);
    QCOMPARE(initialGeo.height(), 32.0);
    QCOMPARE(initialGeo.topLeft(), QPointF(50, 50));

    // Add multiple lines to cause vertical expansion
    root.setFocusOwner(controlPtr);
    sendInputMethod(root, *controlPtr, QStringLiteral("Line 1"), QString());
    keyPress(root, Qt::Key_Return);
    sendInputMethod(root, *controlPtr, QStringLiteral("Line 2"), QString());
    keyPress(root, Qt::Key_Return);
    sendInputMethod(root, *controlPtr, QStringLiteral("Line 3"), QString());
    keyPress(root, Qt::Key_Return);
    sendInputMethod(root, *controlPtr, QStringLiteral("Line 4"), QString());

    const QRectF expandedGeo = controlPtr->geometry();
    // Width stays 200 during live editing; top edge is pinned
    QCOMPARE(expandedGeo.width(), 200.0);
    QCOMPARE(expandedGeo.top(), 50.0);
    QVERIFY(expandedGeo.height() > 32.0);

    // Commit geometry shrinks to text: width = max(ceil(w) + 2*inset, 20)
    QRectF finalRect;
    controlPtr->setOnCommitted([&](const QString&, QRectF r) { finalRect = r; });
    controlPtr->commit();
    QVERIFY(!controlPtr->isEditing());
    QCOMPARE(finalRect.topLeft(), QPointF(50, 50));
    // Shrunk width must be less than 200 and at least 20
    QVERIFY(finalRect.width() <= 200.0);
    QVERIFY(finalRect.width() >= 20.0);
}

// macshot/macshot/UI/Tools/ScopedUndoTextView.swift:9-17@b4d4f3a
void CanvasTextControlTest::scopedTypingUndo() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{100, 100}, 20.0);
    root.setFocusOwner(controlPtr);

    sendInputMethod(root, *controlPtr, QStringLiteral("First"), QString());
    QCOMPARE(controlPtr->text(), QStringLiteral("First"));

    sendInputMethod(root, *controlPtr, QStringLiteral(" Second"), QString());
    QCOMPARE(controlPtr->text(), QStringLiteral("First Second"));

    // Undo typing with Ctrl+Z
    keyPress(root, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(controlPtr->text(), QStringLiteral("First"));

    // Redo typing with Ctrl+Shift+Z
    keyPress(root, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(controlPtr->text(), QStringLiteral("First Second"));
}

void CanvasTextControlTest::accessibleNameAndRole() {
    CanvasTextControl control;
    control.openNew(QPointF{100, 100}, 20.0);
    QCOMPARE(control.accessibleRole(), QAccessible::EditableText);
    QCOMPARE(control.accessibleName(), QStringLiteral("Text Editor"));

    ViewRoot root;
    root.addView(std::make_unique<CanvasTextControl>());
    control.document()->setPlainText(QStringLiteral("Hello world"));
    QCOMPARE(control.accessibleName(), QStringLiteral("Hello world"));
}

// spec 06 §3.2, phase 13 step 2: reports damage for the edited region only
void CanvasTextControlTest::damageReportedForEditedRegionOnly() {
    ViewRoot root;
    root.resize({1920, 1080}, 1.0);

    auto control = std::make_unique<CanvasTextControl>();
    CanvasTextControl* const controlPtr = control.get();
    root.addView(std::move(control));
    controlPtr->openNew(QPointF{200, 200}, 20.0);
    root.setFocusOwner(controlPtr);

    // Initial damage reported should be within the control's paint bounds
    const QRectF damageBefore = controlPtr->lastReportedDamage();
    QVERIFY(!damageBefore.isEmpty());
    QVERIFY(controlPtr->paintBounds().contains(damageBefore));

    // Type text: damage reported must not cover the whole surface (1920x1080)
    sendInputMethod(root, *controlPtr, QStringLiteral("A"), QString());
    const QRectF damageAfter = controlPtr->lastReportedDamage();
    QVERIFY(!damageAfter.isEmpty());
    QVERIFY(controlPtr->paintBounds().contains(damageAfter));
    QVERIFY(damageAfter.width() < 300.0);
    QVERIFY(damageAfter.height() < 100.0);
}

} // namespace ariadshot::ui

QTEST_MAIN(ariadshot::ui::CanvasTextControlTest)
#include "tst_CanvasTextControl.moc"
