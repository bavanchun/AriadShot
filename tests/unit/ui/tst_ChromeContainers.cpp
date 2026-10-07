// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ViewObject.h"
#include "ui/ViewRoot.h"
#include "ui/chrome/ChromeMenu.h"
#include "ui/chrome/Popover.h"
#include "ui/chrome/ToolTipView.h"

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTest>

#include <memory>
#include <vector>

namespace ariadshot::ui {
namespace {

class DummyAnchorView final : public ViewObject {
  public:
    explicit DummyAnchorView(QRectF area) { setGeometry(area); }
    void paint(QPainter&) override {}
    [[nodiscard]] QString accessibleName() const override { return QStringLiteral("Anchor"); }
    [[nodiscard]] QAccessible::Role accessibleRole() const override { return QAccessible::PushButton; }
};

class DummyButtonView final : public ViewObject {
  public:
    explicit DummyButtonView(QRectF area) { setGeometry(area); }
    void paint(QPainter&) override {}
    [[nodiscard]] QString accessibleName() const override { return QStringLiteral("Button"); }
    [[nodiscard]] QAccessible::Role accessibleRole() const override { return QAccessible::PushButton; }
    void handleEvent(const QEvent& event) override {
        if (event.type() == QEvent::MouseButtonPress) {
            clicked = true;
        }
    }
    bool clicked = false;
};

void mouse(ViewRoot& root, QEvent::Type type, QPointF position) {
    const bool isMove = type == QEvent::MouseMove;
    const QMouseEvent event(type, position, position, isMove ? Qt::NoButton : Qt::LeftButton,
                            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    root.dispatch(event);
}

void keyPress(ViewRoot& root, Qt::Key key) {
    const QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    root.dispatch(event);
}

} // namespace

class ChromeContainersTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void popoverOpensAtAnchorAndPositionsCorrectly();
    void popoverFollowsMovingAnchor();
    void popoverDismissesOnOutsideClick();
    void popoverDismissesOnEscape();
    void popoverExposesAccessibleNameAndRole();
    void toolTipOpensAtAnchorAndFollowsMove();
    void toolTipDismissesOnOutsideClickAndEscape();
    void chromeMenuOpensAtAnchorAndSelectsItemsWithKeyboard();
    void chromeMenuClickTriggersActionAndDismisses();
    void chromeMenuDismissesOnOutsideClickAndEscape();
    void chromeMenuExposesAccessibleNameAndRole();
    void outsideClickDismissesOverlayAndTriggersUnderlyingButton();
    void anchorDestroyedWhileOverlayIsOpenDoesNotCrash();
    void chromeMenuExposesAccessibleChildren();
};

// macshot/macshot/UI/Popover/PopoverHelper.swift:14-57@b4d4f3a
// macshot/macshot/UI/Overlay/OverlayView+Popovers.swift:34-41@b4d4f3a
void ChromeContainersTest::popoverOpensAtAnchorAndPositionsCorrectly() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto popover = std::make_unique<Popover>(QStringLiteral("Options"));
    Popover* const popoverPtr = popover.get();
    popoverPtr->setPreferredSize({120, 80});
    root.addView(std::move(popover));

    // Preferred edge: Bottom
    popoverPtr->open(anchorPtr, Popover::Edge::Bottom);
    QVERIFY(popoverPtr->isOpen());
    const QRectF bottomGeo = popoverPtr->geometry();
    QCOMPARE(bottomGeo.size(), QSizeF(120, 80));
    // Placed below anchor with exact 4 pt gap
    QCOMPARE(bottomGeo.top(), anchorPtr->geometry().bottom() + 4.0);
    // Horizontally centered on anchor
    QCOMPARE(bottomGeo.center().x(), anchorPtr->geometry().center().x());

    // Preferred edge: Top
    popoverPtr->open(anchorPtr, Popover::Edge::Top);
    const QRectF topGeo = popoverPtr->geometry();
    QCOMPARE(topGeo.size(), QSizeF(120, 80));
    QCOMPARE(topGeo.bottom(), anchorPtr->geometry().top() - 4.0);
    QCOMPARE(topGeo.center().x(), anchorPtr->geometry().center().x());

    // Preferred edge: Left
    popoverPtr->open(anchorPtr, Popover::Edge::Left);
    const QRectF leftGeo = popoverPtr->geometry();
    QCOMPARE(leftGeo.right(), anchorPtr->geometry().left() - 4.0);

    // Preferred edge: Right
    popoverPtr->open(anchorPtr, Popover::Edge::Right);
    const QRectF rightGeo = popoverPtr->geometry();
    QCOMPARE(rightGeo.left(), anchorPtr->geometry().right() + 4.0);
}

// macshot/macshot/UI/Popover/PopoverHelper.swift:27,53@b4d4f3a
void ChromeContainersTest::popoverFollowsMovingAnchor() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto popover = std::make_unique<Popover>();
    Popover* const popoverPtr = popover.get();
    popoverPtr->setPreferredSize({100, 60});
    root.addView(std::move(popover));
    popoverPtr->open(anchorPtr, Popover::Edge::Bottom);

    const QRectF initialGeo = popoverPtr->geometry();

    // Move the anchor view
    anchorPtr->setGeometry(QRectF{250, 300, 60, 40});
    popoverPtr->followAnchor();

    const QRectF updatedGeo = popoverPtr->geometry();
    QVERIFY(updatedGeo != initialGeo);
    QCOMPARE(updatedGeo.center().x(), anchorPtr->geometry().center().x());
    QCOMPARE(updatedGeo.top(), anchorPtr->geometry().bottom() + 4.0);
}

// macshot/macshot/UI/Popover/PopoverHelper.swift:30,56,65-72@b4d4f3a
void ChromeContainersTest::popoverDismissesOnOutsideClick() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto popover = std::make_unique<Popover>();
    Popover* const popoverPtr = popover.get();
    popoverPtr->setPreferredSize({100, 60});
    root.addView(std::move(popover));
    popoverPtr->open(anchorPtr, Popover::Edge::Bottom);
    QVERIFY(popoverPtr->isOpen());

    // Click outside popover card
    mouse(root, QEvent::MouseButtonPress, QPointF{500, 500});
    QVERIFY(!popoverPtr->isOpen());
}

// macshot/macshot/UI/Popover/PopoverHelper.swift:65-72@b4d4f3a
void ChromeContainersTest::popoverDismissesOnEscape() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto popover = std::make_unique<Popover>();
    Popover* const popoverPtr = popover.get();
    popoverPtr->setPreferredSize({100, 60});
    root.addView(std::move(popover));
    popoverPtr->open(anchorPtr, Popover::Edge::Bottom);
    root.setFocusOwner(popoverPtr);
    QVERIFY(popoverPtr->isOpen());

    keyPress(root, Qt::Key_Escape);
    QVERIFY(!popoverPtr->isOpen());
}

void ChromeContainersTest::popoverExposesAccessibleNameAndRole() {
    Popover popover(QStringLiteral("Recording Options"));
    QCOMPARE(popover.accessibleName(), QStringLiteral("Recording Options"));
    QCOMPARE(popover.accessibleRole(), QAccessible::LayeredPane);
}

void ChromeContainersTest::toolTipOpensAtAnchorAndFollowsMove() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto tip = std::make_unique<ToolTipView>();
    ToolTipView* const tipPtr = tip.get();
    root.addView(std::move(tip));
    tipPtr->open(anchorPtr, QStringLiteral("Pin to screen"), ToolTipView::Edge::Bottom);

    QVERIFY(tipPtr->isOpen());
    QCOMPARE(tipPtr->text(), QStringLiteral("Pin to screen"));
    QCOMPARE(tipPtr->accessibleName(), QStringLiteral("Pin to screen"));
    QCOMPARE(tipPtr->accessibleRole(), QAccessible::ToolTip);

    const QRectF initialGeo = tipPtr->geometry();
    anchorPtr->setGeometry(QRectF{300, 200, 60, 40});
    tipPtr->followAnchor();
    QVERIFY(tipPtr->geometry() != initialGeo);
    QVERIFY(tipPtr->geometry().top() >= anchorPtr->geometry().bottom());
}

void ChromeContainersTest::toolTipDismissesOnOutsideClickAndEscape() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto tip = std::make_unique<ToolTipView>();
    ToolTipView* const tipPtr = tip.get();
    root.addView(std::move(tip));
    tipPtr->open(anchorPtr, QStringLiteral("Help text"));
    QVERIFY(tipPtr->isOpen());

    // Dismiss on outside click
    mouse(root, QEvent::MouseButtonPress, QPointF{400, 400});
    QVERIFY(!tipPtr->isOpen());

    // Reopen and dismiss on Escape
    auto tip2 = std::make_unique<ToolTipView>();
    ToolTipView* const tip2Ptr = tip2.get();
    root.addView(std::move(tip2));
    tip2Ptr->open(anchorPtr, QStringLiteral("Help text"));
    root.setFocusOwner(tip2Ptr);
    QVERIFY(tip2Ptr->isOpen());

    keyPress(root, Qt::Key_Escape);
    QVERIFY(!tip2Ptr->isOpen());
}

void ChromeContainersTest::chromeMenuOpensAtAnchorAndSelectsItemsWithKeyboard() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{50, 50, 40, 30});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto menu = std::make_unique<ChromeMenu>(QStringLiteral("File"));
    ChromeMenu* const menuPtr = menu.get();

    bool action1Triggered = false;
    bool action2Triggered = false;
    menuPtr->addAction(QStringLiteral("Save"), [&]() { action1Triggered = true; });
    menuPtr->addSeparator();
    menuPtr->addAction(QStringLiteral("Save As"), [&]() { action2Triggered = true; });
    QCOMPARE(menuPtr->itemCount(), 3);

    root.addView(std::move(menu));
    menuPtr->open(anchorPtr, ChromeMenu::Edge::Bottom);
    root.setFocusOwner(menuPtr);
    QVERIFY(menuPtr->isOpen());

    // Navigate with Down arrow: selects index 0 ("Save")
    keyPress(root, Qt::Key_Down);
    QCOMPARE(menuPtr->hoveredIndex(), 0);

    // Down arrow skips separator (index 1), selects index 2 ("Save As")
    keyPress(root, Qt::Key_Down);
    QCOMPARE(menuPtr->hoveredIndex(), 2);

    // Up arrow returns to index 0 ("Save")
    keyPress(root, Qt::Key_Up);
    QCOMPARE(menuPtr->hoveredIndex(), 0);

    // Enter triggers index 0
    keyPress(root, Qt::Key_Return);
    QVERIFY(action1Triggered);
    QVERIFY(!action2Triggered);
    QVERIFY(!menuPtr->isOpen()); // Closes after trigger
}

void ChromeContainersTest::chromeMenuClickTriggersActionAndDismisses() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{50, 50, 40, 30});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto menu = std::make_unique<ChromeMenu>(QStringLiteral("Edit"));
    ChromeMenu* const menuPtr = menu.get();

    bool actionTriggered = false;
    menuPtr->addAction(QStringLiteral("Copy"), [&]() { actionTriggered = true; });

    root.addView(std::move(menu));
    menuPtr->open(anchorPtr, ChromeMenu::Edge::Bottom);
    QVERIFY(menuPtr->isOpen());

    // Click inside the menu item
    const QPointF itemPos = menuPtr->geometry().center();
    mouse(root, QEvent::MouseButtonPress, itemPos);
    QVERIFY(actionTriggered);
    QVERIFY(!menuPtr->isOpen());
}

void ChromeContainersTest::chromeMenuDismissesOnOutsideClickAndEscape() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{50, 50, 40, 30});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto menu = std::make_unique<ChromeMenu>();
    ChromeMenu* const menuPtr = menu.get();
    menuPtr->addAction(QStringLiteral("Option"));

    root.addView(std::move(menu));
    menuPtr->open(anchorPtr, ChromeMenu::Edge::Bottom);
    QVERIFY(menuPtr->isOpen());

    // Dismiss on outside click
    mouse(root, QEvent::MouseButtonPress, QPointF{500, 500});
    QVERIFY(!menuPtr->isOpen());

    // Reopen and dismiss on Escape
    auto menu2 = std::make_unique<ChromeMenu>();
    ChromeMenu* const menu2Ptr = menu2.get();
    menu2Ptr->addAction(QStringLiteral("Option"));
    root.addView(std::move(menu2));
    menu2Ptr->open(anchorPtr, ChromeMenu::Edge::Bottom);
    root.setFocusOwner(menu2Ptr);
    QVERIFY(menu2Ptr->isOpen());

    keyPress(root, Qt::Key_Escape);
    QVERIFY(!menu2Ptr->isOpen());
}

void ChromeContainersTest::chromeMenuExposesAccessibleNameAndRole() {
    ChromeMenu menu(QStringLiteral("Tools"));
    QCOMPARE(menu.accessibleName(), QStringLiteral("Tools"));
    QCOMPARE(menu.accessibleRole(), QAccessible::PopupMenu);
}

void ChromeContainersTest::outsideClickDismissesOverlayAndTriggersUnderlyingButton() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto button = std::make_unique<DummyButtonView>(QRectF{450, 450, 100, 40});
    DummyButtonView* const buttonPtr = button.get();
    root.addView(std::move(button));

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    // Test Popover dismisses and lets underlying button receive press
    auto popover = std::make_unique<Popover>();
    Popover* const popoverPtr = popover.get();
    popoverPtr->setPreferredSize({100, 60});
    root.addView(std::move(popover));
    popoverPtr->open(anchorPtr, Popover::Edge::Bottom);
    QVERIFY(popoverPtr->isOpen());

    buttonPtr->clicked = false;
    mouse(root, QEvent::MouseButtonPress, QPointF{500, 470});
    QVERIFY(!popoverPtr->isOpen());
    QVERIFY(buttonPtr->clicked);

    // Test ToolTipView dismisses and lets underlying button receive press
    auto tip = std::make_unique<ToolTipView>();
    ToolTipView* const tipPtr = tip.get();
    root.addView(std::move(tip));
    tipPtr->open(anchorPtr, QStringLiteral("Tip"));
    QVERIFY(tipPtr->isOpen());

    buttonPtr->clicked = false;
    mouse(root, QEvent::MouseButtonPress, QPointF{500, 470});
    QVERIFY(!tipPtr->isOpen());
    QVERIFY(buttonPtr->clicked);

    // Test ChromeMenu dismisses and lets underlying button receive press
    auto menu = std::make_unique<ChromeMenu>();
    ChromeMenu* const menuPtr = menu.get();
    menuPtr->addAction(QStringLiteral("Item"));
    root.addView(std::move(menu));
    menuPtr->open(anchorPtr, ChromeMenu::Edge::Bottom);
    QVERIFY(menuPtr->isOpen());

    buttonPtr->clicked = false;
    mouse(root, QEvent::MouseButtonPress, QPointF{500, 470});
    QVERIFY(!menuPtr->isOpen());
    QVERIFY(buttonPtr->clicked);
}

void ChromeContainersTest::anchorDestroyedWhileOverlayIsOpenDoesNotCrash() {
    ViewRoot root;
    root.resize({800, 600}, 1.0);

    auto anchor = std::make_unique<DummyAnchorView>(QRectF{100, 100, 60, 40});
    DummyAnchorView* const anchorPtr = anchor.get();
    root.addView(std::move(anchor));

    auto popover = std::make_unique<Popover>();
    Popover* const popoverPtr = popover.get();
    popoverPtr->setPreferredSize({100, 60});
    root.addView(std::move(popover));
    popoverPtr->open(anchorPtr, Popover::Edge::Bottom);

    auto tip = std::make_unique<ToolTipView>();
    ToolTipView* const tipPtr = tip.get();
    root.addView(std::move(tip));
    tipPtr->open(anchorPtr, QStringLiteral("Tip"));

    auto menu = std::make_unique<ChromeMenu>();
    ChromeMenu* const menuPtr = menu.get();
    menuPtr->addAction(QStringLiteral("Item"));
    root.addView(std::move(menu));
    menuPtr->open(anchorPtr, ChromeMenu::Edge::Bottom);

    QCOMPARE(popoverPtr->anchorView(), anchorPtr);
    QCOMPARE(tipPtr->anchorView(), anchorPtr);
    QCOMPARE(menuPtr->anchorView(), anchorPtr);

    // Destroy the anchor view by removing it from root
    root.removeView(*anchorPtr);

    // Overlays must safely detect expired weak ref and not crash on followAnchor
    popoverPtr->followAnchor();
    tipPtr->followAnchor();
    menuPtr->followAnchor();

    QVERIFY(popoverPtr->anchorView() == nullptr);
    QVERIFY(tipPtr->anchorView() == nullptr);
    QVERIFY(menuPtr->anchorView() == nullptr);
}

void ChromeContainersTest::chromeMenuExposesAccessibleChildren() {
    ChromeMenu emptyMenu;
    QCOMPARE(emptyMenu.accessibleName(), QStringLiteral("Menu"));

    ChromeMenu menu(QStringLiteral("Edit"));
    bool cutTriggered = false;
    menu.addAction(QStringLiteral("Cut"), [&]() { cutTriggered = true; }, QStringLiteral("Ctrl+X"));
    menu.addSeparator();
    menu.addAction(QStringLiteral("Paste"));

    QAccessibleInterface* const menuIface = menu.accessible();
    QVERIFY(menuIface != nullptr);
    QCOMPARE(menuIface->role(), QAccessible::PopupMenu);
    QCOMPARE(menuIface->text(QAccessible::Name), QStringLiteral("Edit"));
    QCOMPARE(menuIface->childCount(), 3);

    QAccessibleInterface* const item0 = menuIface->child(0);
    if (!item0) {
        QFAIL("item0 is null");
    }
    QCOMPARE(item0->role(), QAccessible::MenuItem);
    QCOMPARE(item0->text(QAccessible::Name), QStringLiteral("Cut"));
    QCOMPARE(item0->text(QAccessible::Accelerator), QStringLiteral("Ctrl+X"));
    QCOMPARE(menuIface->indexOfChild(item0), 0);

    QAccessibleActionInterface* const actionIface = item0->actionInterface();
    if (!actionIface) {
        QFAIL("actionIface is null");
    }
    QVERIFY(actionIface->actionNames().contains(QAccessibleActionInterface::pressAction()));
    actionIface->doAction(QAccessibleActionInterface::pressAction());
    QVERIFY(cutTriggered);

    QAccessibleInterface* const item1 = menuIface->child(1);
    if (!item1) {
        QFAIL("item1 is null");
    }
    QCOMPARE(item1->role(), QAccessible::Separator);

    QAccessibleInterface* const item2 = menuIface->child(2);
    if (!item2) {
        QFAIL("item2 is null");
    }
    QCOMPARE(item2->role(), QAccessible::MenuItem);
    QCOMPARE(item2->text(QAccessible::Name), QStringLiteral("Paste"));
}

} // namespace ariadshot::ui

QTEST_MAIN(ariadshot::ui::ChromeContainersTest)
#include "tst_ChromeContainers.moc"
