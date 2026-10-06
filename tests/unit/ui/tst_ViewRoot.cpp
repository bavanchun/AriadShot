// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "platform/SurfaceHost.h"
#include "ui/ViewObject.h"
#include "ui/ViewRoot.h"

#include <QAccessible>
#include <QAccessibleInterface>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QImage>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTest>
#include <QWheelEvent>

#include <functional>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using ariadshot::platform::HostedSurface;
using ariadshot::platform::KeyboardInteractivity;
using ariadshot::platform::OutputId;
using ariadshot::platform::SurfaceContent;
using ariadshot::platform::SurfaceHost;
using ariadshot::ui::ViewObject;
using ariadshot::ui::ViewRoot;

namespace {

using Events = std::vector<QEvent::Type>;

// A view object that records what the root does to it.
class TestView final : public ViewObject {
  public:
    explicit TestView(QRectF area, QString viewName = {}) : name(std::move(viewName)) { setGeometry(area); }

    void paint(QPainter& painter) override {
        ++paintCount;
        painter.fillRect(paintArea.isNull() ? geometry() : paintArea, color);
    }
    ViewObject* hitTest(QPointF point) override { return passThrough ? nullptr : ViewObject::hitTest(point); }
    void hoverChanged(bool hovered) override {
        hovers.push_back(hovered);
        if (onHover) {
            onHover(hovered);
        }
    }
    void handleEvent(const QEvent& event) override {
        events.push_back(event.type());
        if (event.isSinglePointEvent()) {
            positions.push_back(static_cast<const QSinglePointEvent&>(event).position());
        }
    }
    void inputMethodQuery(QInputMethodQueryEvent& query) override {
        queried = query.queries();
        query.setValue(Qt::ImEnabled, true);
        query.setValue(Qt::ImSurroundingText, surroundingText);
        query.setValue(Qt::ImCursorPosition, cursorPosition);
    }
    std::optional<Qt::CursorShape> cursorAt(QPointF) const override { return cursor; }
    QString accessibleName() const override { return name; }
    QAccessible::Role accessibleRole() const override { return role; }

    QString name;
    QAccessible::Role role = QAccessible::PushButton;
    std::optional<Qt::CursorShape> cursor = Qt::CrossCursor;
    QColor color = Qt::red;
    QRectF paintArea; // where paint() draws; the geometry when null
    QString surroundingText;
    int cursorPosition = 0;
    bool passThrough = false;
    int paintCount = 0;
    std::vector<bool> hovers;
    Events events;
    std::vector<QPointF> positions;
    Qt::InputMethodQueries queried;
    std::function<void(bool)> onHover;
};

// A view object whose hit test answers with a descendant that the root does not hold.
class ParentView final : public ViewObject {
  public:
    ParentView(QRectF area, ViewObject& hitChild) : child(&hitChild) { setGeometry(area); }

    void paint(QPainter&) override {}
    ViewObject* hitTest(QPointF point) override { return geometry().contains(point) ? child : nullptr; }
    QString accessibleName() const override { return {}; }
    QAccessible::Role accessibleRole() const override { return QAccessible::Grouping; }

    ViewObject* child;
};

TestView& addView(ViewRoot& root, QRectF area) {
    return static_cast<TestView&>(root.addView(std::make_unique<TestView>(area)));
}

void mouse(ViewRoot& root, QEvent::Type type, QPointF position) {
    const bool isMove = type == QEvent::MouseMove;
    const QMouseEvent event(type, position, position, isMove ? Qt::NoButton : Qt::LeftButton,
                            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    root.dispatch(event);
}

// A move with the left button held.
void drag(ViewRoot& root, QPointF position) {
    root.dispatch(QMouseEvent(QEvent::MouseMove, position, position, Qt::NoButton, Qt::LeftButton, Qt::NoModifier));
}

// The host's accessible root: its one child is the content's accessible interface, as a host window would add it.
class HostAccessible final : public QAccessibleInterface {
  public:
    explicit HostAccessible(SurfaceContent& surfaceContent) : m_content(surfaceContent) {}

    bool isValid() const override { return true; }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    QAccessibleInterface* parent() const override { return nullptr; }
    QAccessibleInterface* child(int index) const override { return index == 0 ? m_content.accessible() : nullptr; }
    int childCount() const override { return 1; }
    int indexOfChild(const QAccessibleInterface* candidate) const override {
        return candidate == m_content.accessible() ? 0 : -1;
    }
    QString text(QAccessible::Text) const override { return {}; }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return {}; }
    QAccessible::Role role() const override { return QAccessible::Window; }
    QAccessible::State state() const override { return {}; }

  private:
    SurfaceContent& m_content;
};

// The host side of the platform interfaces: it shows what the content reports.
class FakeSurface final : public HostedSurface {
  public:
    FakeSurface(SurfaceHost::Role surfaceRole, OutputId surfaceOutput, SurfaceContent& surfaceContent)
        : role(surfaceRole), output(std::move(surfaceOutput)), content(surfaceContent) {}
    void map() override { setMapped(true); }
    void unmap() override { setMapped(false); }
    bool isMapped() const override { return m_mapped; }
    void setMappedHandler(std::function<void(bool)> handler) override { m_onMapped = std::move(handler); }

    SurfaceHost::Role role;
    OutputId output;
    SurfaceContent& content;

  private:
    void setMapped(bool value) {
        m_mapped = value;
        if (m_onMapped) {
            m_onMapped(value);
        }
    }
    bool m_mapped = false;
    std::function<void(bool)> m_onMapped;
};

class FakeHost final : public SurfaceHost {
  public:
    std::unique_ptr<HostedSurface> create(Role role, OutputId output, SurfaceContent& content) override {
        auto surface = std::make_unique<FakeSurface>(role, std::move(output), content);
        last = surface.get();
        return surface;
    }
    FakeSurface* last = nullptr;
};

} // namespace

class ViewRootTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void pointerEventsGoToTheFrontmostViewUnderThePointer();
    void pointerOwnerReceivesTheDragAndReleaseOutsideItsBounds();
    void moveWithNoButtonHeldEndsAStaleDrag();
    void removingThePointerOwnerEndsTheDrag();
    void removingTheHolderOfAPointerOwnerEndsTheDrag();
    void passThroughViewLetsTheViewBehindReceiveTheEvent();
    void wheelAndTabletEventsRouteLikeMouseEvents();
    void hoverFollowsThePointer();
    void viewRemovingItselfWhenHoverEndsIsNotNotifiedTwice();
    void pointerEventSkipsAViewRemovedByAHoverCallback();
    void keyAndFocusEventsGoToTheFocusOwner();
    void inputMethodEventsGoToTheInputMethodOwner();
    void inputMethodQueriesAreAnsweredByTheInputMethodOwner();
    void inputMethodIsDisabledWithoutAnOwner();
    void removedViewIsForgotten();
    void updateAccumulatesDamageUntilTaken();
    void layersAreCanonicalLayersThenChrome();
    void chromeBufferCoversTheSurfaceAtFractionalScale();
    void chromeLayerIsRepaintedOverTheDamageOnly();
    void chromeIsRepaintedOverViewsPaintingOutsideTheirGeometry();
    void cursorFollowsTheViewUnderThePointer();
    void keyboardModeIsReported();
    void viewObjectExposesAccessibleNameAndRole();
    void hostReachesTheAccessibleViewsAndLocatesThemOnTheScreen();
    void hostPresentsContentThroughThePlatformInterfaces();
};

void ViewRootTest::pointerEventsGoToTheFrontmostViewUnderThePointer() {
    ViewRoot root;
    TestView& back = addView(root, {0, 0, 100, 100});
    TestView& front = addView(root, {50, 50, 100, 100});

    mouse(root, QEvent::MouseButtonPress, {75, 75});
    mouse(root, QEvent::MouseButtonRelease, {75, 75});
    mouse(root, QEvent::MouseButtonPress, {10, 10});
    mouse(root, QEvent::MouseButtonRelease, {10, 10});
    mouse(root, QEvent::MouseMove, {500, 500});

    QCOMPARE(front.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
    QCOMPARE(back.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
}

// The view that took the press keeps the drag and the release wherever the pointer is: ToolbarButtonView forwards
// mouseDragged and mouseUp to the view that got mouseDown, and its mouseUp clears the pressed state even outside the
// bounds (macshot/UI/Toolbar/ToolbarButtonView.swift:268-295@b4d4f3a).
void ViewRootTest::pointerOwnerReceivesTheDragAndReleaseOutsideItsBounds() {
    ViewRoot root;
    TestView& button = addView(root, {0, 0, 50, 50});
    TestView& other = addView(root, {60, 0, 50, 50});

    mouse(root, QEvent::MouseButtonPress, {10, 10});
    drag(root, {70, 10});
    mouse(root, QEvent::MouseButtonRelease, {500, 500});

    QCOMPARE(button.events, (Events{QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}));
    QCOMPARE(button.positions, (std::vector<QPointF>{{10, 10}, {70, 10}, {500, 500}}));
    QVERIFY(other.events.empty());

    mouse(root, QEvent::MouseMove, {70, 10});
    QCOMPARE(other.events, Events{QEvent::MouseMove});
    QCOMPARE(button.events.size(), std::size_t{3});
}

void ViewRootTest::moveWithNoButtonHeldEndsAStaleDrag() {
    ViewRoot root;
    TestView& button = addView(root, {0, 0, 50, 50});
    TestView& other = addView(root, {60, 0, 50, 50});

    mouse(root, QEvent::MouseButtonPress, {10, 10});
    mouse(root, QEvent::MouseMove, {70, 10}); // the release was lost: no button is held any more

    QCOMPARE(button.events, Events{QEvent::MouseButtonPress});
    QCOMPARE(other.events, Events{QEvent::MouseMove});
}

void ViewRootTest::removingThePointerOwnerEndsTheDrag() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    const std::unique_ptr<ViewObject> removed = root.removeView(owner);
    mouse(root, QEvent::MouseButtonRelease, {15, 15});

    QCOMPARE(owner.events, Events{QEvent::MouseButtonPress});
    QCOMPARE(behind.events, Events{QEvent::MouseButtonRelease});
}

void ViewRootTest::removingTheHolderOfAPointerOwnerEndsTheDrag() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView child({0, 0, 0, 0});
    ViewObject& holder = root.addView(std::make_unique<ParentView>(QRectF(10, 10, 20, 20), child));

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    const std::unique_ptr<ViewObject> removed = root.removeView(holder);
    drag(root, {70, 70});

    QCOMPARE(child.events, Events{QEvent::MouseButtonPress});
    QCOMPARE(child.hovers, (std::vector<bool>{true, false}));
    QCOMPARE(behind.events, Events{QEvent::MouseMove});
}

void ViewRootTest::passThroughViewLetsTheViewBehindReceiveTheEvent() {
    ViewRoot root;
    TestView& back = addView(root, {0, 0, 100, 100});
    TestView& front = addView(root, {0, 0, 100, 100});
    front.passThrough = true;

    mouse(root, QEvent::MouseButtonPress, {10, 10});

    QVERIFY(front.events.empty());
    QCOMPARE(back.events, Events{QEvent::MouseButtonPress});
}

void ViewRootTest::wheelAndTabletEventsRouteLikeMouseEvents() {
    ViewRoot root;
    TestView& view = addView(root, {0, 0, 100, 100});
    const QPointF at(20, 20);
    const QPointingDevice stylus(u"stylus"_s, 1, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                 QInputDevice::Capability::Position, 1, 1);

    root.dispatch(QWheelEvent(at, at, {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false));
    for (const QEvent::Type type : {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}) {
        const Qt::MouseButtons held = type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton;
        root.dispatch(QTabletEvent(type, &stylus, at, at, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, held));
    }

    QCOMPARE(view.events, (Events{QEvent::Wheel, QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}));
}

void ViewRootTest::hoverFollowsThePointer() {
    ViewRoot root;
    TestView& first = addView(root, {0, 0, 50, 50});
    TestView& second = addView(root, {60, 0, 50, 50});

    mouse(root, QEvent::MouseMove, {10, 10});
    mouse(root, QEvent::MouseMove, {20, 20});
    QCOMPARE(first.hovers, std::vector<bool>{true});

    mouse(root, QEvent::MouseMove, {70, 10});
    QCOMPARE(first.hovers, (std::vector<bool>{true, false}));
    QCOMPARE(second.hovers, std::vector<bool>{true});

    mouse(root, QEvent::MouseMove, {500, 500});
    QCOMPARE(second.hovers, (std::vector<bool>{true, false}));

    root.dispatch(QEnterEvent({10, 10}, {10, 10}, {10, 10}));
    QCOMPARE(first.hovers, (std::vector<bool>{true, false, true}));
    root.dispatch(QEvent(QEvent::Leave));
    QCOMPARE(first.hovers, (std::vector<bool>{true, false, true, false}));
}

void ViewRootTest::viewRemovingItselfWhenHoverEndsIsNotNotifiedTwice() {
    ViewRoot root;
    TestView& first = addView(root, {0, 0, 50, 50});
    TestView& second = addView(root, {60, 0, 50, 50});
    std::unique_ptr<ViewObject> removed;
    first.onHover = [&](bool hovered) {
        if (!hovered && !removed) {
            removed = root.removeView(first);
        }
    };
    mouse(root, QEvent::MouseMove, {10, 10});

    mouse(root, QEvent::MouseMove, {70, 10});

    QVERIFY(removed != nullptr);
    QCOMPARE(first.hovers, (std::vector<bool>{true, false}));
    QCOMPARE(second.hovers, std::vector<bool>{true});
    QCOMPARE(second.events, Events{QEvent::MouseMove});
    mouse(root, QEvent::MouseMove, {500, 500});
    QCOMPARE(second.hovers, (std::vector<bool>{true, false}));
}

void ViewRootTest::pointerEventSkipsAViewRemovedByAHoverCallback() {
    ViewRoot root;
    TestView& back = addView(root, {0, 0, 100, 100});
    TestView& front = addView(root, {0, 0, 100, 100});
    std::unique_ptr<ViewObject> removed;
    front.onHover = [&](bool hovered) {
        if (hovered && !removed) {
            removed = root.removeView(front);
        }
    };

    mouse(root, QEvent::MouseButtonPress, {10, 10});

    QVERIFY(removed != nullptr);
    QVERIFY(front.events.empty());
    QCOMPARE(front.hovers, (std::vector<bool>{true, false}));
    QCOMPARE(back.hovers, std::vector<bool>{true});
    QCOMPARE(back.events, Events{QEvent::MouseButtonPress});
}

void ViewRootTest::keyAndFocusEventsGoToTheFocusOwner() {
    ViewRoot root;
    TestView& first = addView(root, {0, 0, 10, 10});
    TestView& second = addView(root, {20, 0, 10, 10});
    const QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    const QFocusEvent focusOut(QEvent::FocusOut);

    root.dispatch(press);
    root.setFocusOwner(&first);
    root.dispatch(press);
    root.dispatch(focusOut);
    root.setFocusOwner(&second);
    root.dispatch(press);
    root.setFocusOwner(nullptr);
    root.dispatch(press);

    QCOMPARE(first.events, (Events{QEvent::KeyPress, QEvent::FocusOut}));
    QCOMPARE(second.events, Events{QEvent::KeyPress});
}

void ViewRootTest::inputMethodEventsGoToTheInputMethodOwner() {
    ViewRoot root;
    TestView& canvas = addView(root, {0, 0, 100, 100});
    TestView& text = addView(root, {10, 10, 30, 10});
    root.setFocusOwner(&canvas);
    root.setInputMethodOwner(&text);

    root.dispatch(QInputMethodEvent(u"a"_s, {}));
    root.dispatch(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));

    QCOMPARE(text.events, Events{QEvent::InputMethod});
    QCOMPARE(canvas.events, Events{QEvent::KeyPress});
}

// Qt asks the focus object for surrounding text, the cursor position and so on, and the receiver answers with
// setValue() on the event (https://doc.qt.io/qt-6.8/qinputmethodqueryevent.html).
void ViewRootTest::inputMethodQueriesAreAnsweredByTheInputMethodOwner() {
    ViewRoot root;
    TestView& canvas = addView(root, {0, 0, 100, 100});
    TestView& text = addView(root, {10, 10, 30, 10});
    text.surroundingText = u"hello"_s;
    text.cursorPosition = 3;
    root.setFocusOwner(&canvas);
    root.setInputMethodOwner(&text);
    const Qt::InputMethodQueries asked = Qt::ImEnabled | Qt::ImSurroundingText | Qt::ImCursorPosition;
    QInputMethodQueryEvent query(asked);

    static_cast<SurfaceContent&>(root).inputMethodQuery(query);

    QCOMPARE(text.queried, asked);
    QCOMPARE(query.value(Qt::ImEnabled), QVariant(true));
    QCOMPARE(query.value(Qt::ImSurroundingText), QVariant(u"hello"_s));
    QCOMPARE(query.value(Qt::ImCursorPosition), QVariant(3));
    QVERIFY(canvas.events.empty());
    QVERIFY(canvas.queried == Qt::InputMethodQueries());
}

void ViewRootTest::inputMethodIsDisabledWithoutAnOwner() {
    ViewRoot root;
    QInputMethodQueryEvent query(Qt::ImEnabled);

    root.inputMethodQuery(query);

    QCOMPARE(query.value(Qt::ImEnabled), QVariant(false));
}

void ViewRootTest::removedViewIsForgotten() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    TestView& view = addView(root, {0, 0, 50, 50});
    mouse(root, QEvent::MouseMove, {10, 10});
    root.setFocusOwner(&view);
    root.setInputMethodOwner(&view);
    (void)root.takeDamage();

    const std::unique_ptr<ViewObject> removed = root.removeView(view);

    QVERIFY(removed != nullptr);
    QCOMPARE(view.hovers, (std::vector<bool>{true, false}));
    QCOMPARE(root.takeDamage(), QRegion(0, 0, 50, 50));
    root.dispatch(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
    root.dispatch(QInputMethodEvent(u"a"_s, {}));
    mouse(root, QEvent::MouseMove, {10, 10});
    QCOMPARE(view.events, Events{QEvent::MouseMove}); // only the move that made it hovered, before the removal
    view.update({0, 0, 5, 5});
    QVERIFY(root.takeDamage().isEmpty());
    QVERIFY(root.removeView(view) == nullptr);
}

void ViewRootTest::updateAccumulatesDamageUntilTaken() {
    ViewRoot root;
    root.resize({200, 100}, 1.0);
    QCOMPARE(root.takeDamage(), QRegion(0, 0, 200, 100));
    QVERIFY(root.takeDamage().isEmpty());

    TestView& view = addView(root, {10, 10, 20, 20});
    QCOMPARE(root.takeDamage(), QRegion(10, 10, 20, 20));

    view.update({0.5, 0.5, 1, 1});
    view.update({1, 1, 2, 2});
    QCOMPARE(root.takeDamage(), QRegion(0, 0, 2, 2).united(QRegion(1, 1, 2, 2)));
    QVERIFY(root.takeDamage().isEmpty());

    view.update({300, 300, 10, 10});
    QVERIFY(root.takeDamage().isEmpty());

    view.setGeometry({20, 20, 20, 20});
    QCOMPARE(root.takeDamage(), QRegion(10, 10, 20, 20).united(QRegion(20, 20, 20, 20)));
}

void ViewRootTest::layersAreCanonicalLayersThenChrome() {
    ViewRoot root;
    const QImage first(2, 2, QImage::Format_ARGB32_Premultiplied);
    const QImage second(2, 2, QImage::Format_ARGB32_Premultiplied);

    root.setCanonicalLayers({&first, &second});
    QCOMPARE(root.presentationLayers(), (std::vector<const QImage*>{&first, &second}));

    root.resize({20, 10}, 2.0);
    const std::vector<const QImage*> layers = root.presentationLayers();
    QCOMPARE(layers.size(), std::size_t{3});
    QCOMPARE(layers[0], &first);
    QCOMPARE(layers[1], &second);
    QCOMPARE(layers[2]->size(), QSize(40, 20));
    QCOMPARE(layers[2]->devicePixelRatio(), 2.0);
    QCOMPARE(root.takeDamage(), QRegion(0, 0, 20, 10));
}

// The buffer must cover the whole surface: 101 points at 1.25 is 126.25 pixels, and 126 pixels would stop at 100.8
// points (https://doc.qt.io/qt-6.8/highdpi.html: logical size is pixels over device pixel ratio).
void ViewRootTest::chromeBufferCoversTheSurfaceAtFractionalScale() {
    ViewRoot root;

    root.resize({101, 51}, 1.25);

    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.size(), QSize(127, 64));
    QCOMPARE(chrome.devicePixelRatio(), 1.25);
    QVERIFY(chrome.deviceIndependentSize().width() >= 101);
    QVERIFY(chrome.deviceIndependentSize().height() >= 51);
}

void ViewRootTest::chromeLayerIsRepaintedOverTheDamageOnly() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    TestView& first = addView(root, {10, 10, 20, 20});
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(15, 15), QColor(Qt::red));
    QCOMPARE(chrome.pixelColor(50, 50).alpha(), 0);

    TestView& second = addView(root, {60, 60, 10, 10});
    second.color = Qt::green;
    first.color = Qt::blue; // no update() for the first view: the clip keeps the repaint away from it
    (void)root.takeDamage();
    QCOMPARE(chrome.pixelColor(65, 65), QColor(Qt::green));
    QCOMPARE(chrome.pixelColor(15, 15), QColor(Qt::red));

    first.update(first.geometry());
    (void)root.takeDamage();
    QCOMPARE(chrome.pixelColor(15, 15), QColor(Qt::blue));
    QCOMPARE(chrome.pixelColor(65, 65), QColor(Qt::green));
    const int paints = first.paintCount;
    QVERIFY(root.takeDamage().isEmpty());
    QCOMPARE(first.paintCount, paints);
}

// paint() is not confined to geometry(): a border or a shadow lies outside the hit box, and repainting that area has to
// bring the stroke back after it is cleared.
void ViewRootTest::chromeIsRepaintedOverViewsPaintingOutsideTheirGeometry() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    TestView& view = addView(root, {40, 40, 20, 20});
    view.paintArea = {10, 40, 20, 20};
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(15, 45), QColor(Qt::red));

    view.update({10, 40, 20, 20});
    (void)root.takeDamage();

    QCOMPARE(chrome.pixelColor(15, 45), QColor(Qt::red));
}

void ViewRootTest::cursorFollowsTheViewUnderThePointer() {
    ViewRoot root;
    TestView& button = addView(root, {0, 0, 50, 50});
    button.cursor = Qt::PointingHandCursor;
    TestView& canvas = addView(root, {60, 0, 50, 50});
    canvas.cursor = std::nullopt;

    QCOMPARE(root.cursorAt({10, 10}), std::optional<Qt::CursorShape>(Qt::PointingHandCursor));
    QCOMPARE(root.cursorAt({70, 10}), std::optional<Qt::CursorShape>());
    QCOMPARE(root.cursorAt({500, 500}), std::optional<Qt::CursorShape>(Qt::ArrowCursor));

    mouse(root, QEvent::MouseMove, {70, 10});
    QCOMPARE(root.cursor(), std::optional<Qt::CursorShape>());
    mouse(root, QEvent::MouseMove, {10, 10});
    QCOMPARE(root.cursor(), std::optional<Qt::CursorShape>(Qt::PointingHandCursor));
}

void ViewRootTest::keyboardModeIsReported() {
    ViewRoot root;
    QCOMPARE(root.keyboard(), KeyboardInteractivity::None);
    root.setKeyboard(KeyboardInteractivity::Exclusive);
    QCOMPARE(root.keyboard(), KeyboardInteractivity::Exclusive);
    root.setKeyboard(KeyboardInteractivity::OnDemand);
    QCOMPARE(root.keyboard(), KeyboardInteractivity::OnDemand);
}

void ViewRootTest::viewObjectExposesAccessibleNameAndRole() {
    TestView view({0, 0, 10, 10}, u"Save"_s);
    view.role = QAccessible::Button;

    QAccessibleInterface* accessible = view.accessible();
    if (accessible == nullptr) {
        QFAIL("the view object has no accessible interface");
    }
    QVERIFY(accessible->isValid());
    QCOMPARE(accessible->text(QAccessible::Name), u"Save"_s);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->rect(), QRect(0, 0, 10, 10));
    QVERIFY(accessible->parent() == nullptr);
    view.name = u"Copy"_s;
    QCOMPARE(accessible->text(QAccessible::Name), u"Copy"_s);
    QCOMPARE(view.accessible(), accessible);
}

// Assistive technology walks the tree from the host's window: host root, the content's interface, then its views. The
// host also supplies where the surface is on the screen, and a surface can move, so the mapping is asked at each call
// (https://doc.qt.io/qt-6.8/qaccessibleinterface.html: rect() is in screen coordinates).
void ViewRootTest::hostReachesTheAccessibleViewsAndLocatesThemOnTheScreen() {
    ViewRoot root;
    root.resize({200, 100}, 1.0);
    root.addView(std::make_unique<TestView>(QRectF(10, 10, 20, 20), u"Save"_s));
    root.addView(std::make_unique<TestView>(QRectF(50, 10, 20, 20), u"Copy"_s));
    HostAccessible host(root);
    QPoint origin(1920, 100);
    static_cast<SurfaceContent&>(root).attachAccessible(&host, [&origin](QPoint point) { return point + origin; });

    QAccessibleInterface* content = host.child(0);
    if (content == nullptr) {
        QFAIL("the content has no accessible interface");
    }
    QVERIFY(content->parent() == &host);
    QCOMPARE(content->rect(), QRect(1920, 100, 200, 100));
    QCOMPARE(content->childCount(), 2);
    QAccessibleInterface* save = content->child(0);
    QAccessibleInterface* copy = content->child(1);
    if (save == nullptr || copy == nullptr) {
        QFAIL("the content does not list its views");
    }
    QCOMPARE(save->text(QAccessible::Name), u"Save"_s);
    QCOMPARE(copy->text(QAccessible::Name), u"Copy"_s);
    QVERIFY(save->parent() == content);
    QCOMPARE(content->indexOfChild(copy), 1);
    QCOMPARE(save->rect(), QRect(1930, 110, 20, 20));
    QVERIFY(content->childAt(1935, 115) == save);
    QVERIFY(content->childAt(1925, 105) == nullptr);

    origin = {0, 50};
    QCOMPARE(copy->rect(), QRect(50, 60, 20, 20));
}

void ViewRootTest::hostPresentsContentThroughThePlatformInterfaces() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    addView(root, {0, 0, 10, 10});
    FakeHost host;

    const std::unique_ptr<HostedSurface> surface = host.create(SurfaceHost::Role::Overlay, OutputId{u"DP-1"_s}, root);
    QVERIFY(host.last != nullptr);
    QCOMPARE(host.last->role, SurfaceHost::Role::Overlay);
    QCOMPARE(host.last->output, OutputId{u"DP-1"_s});

    std::vector<bool> mapped;
    surface->setMappedHandler([&mapped](bool value) { mapped.push_back(value); });
    surface->map();
    surface->unmap();
    QCOMPARE(mapped, (std::vector<bool>{true, false}));
    QVERIFY(!surface->isMapped());

    SurfaceContent& content = host.last->content;
    QCOMPARE(content.takeDamage(), QRegion(0, 0, 100, 100));
    QCOMPARE(content.presentationLayers().size(), std::size_t{1});
}

QTEST_MAIN(ViewRootTest)
#include "tst_ViewRoot.moc"
