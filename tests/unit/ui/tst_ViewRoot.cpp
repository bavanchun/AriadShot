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
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformaccessibility.h>
#include <QtGui/qpa/qplatformintegration.h>

#include <functional>
#include <memory>
#include <ranges>
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
    ~TestView() override {
        if (onDestroy) {
            onDestroy();
        }
    }

    QRectF paintBounds() const override { return geometry().adjusted(-shadow, -shadow, shadow, shadow); }
    void paint(QPainter& painter) override {
        ++paintCount;
        const qreal reach = shadow + overdraw;
        painter.fillRect(geometry().adjusted(-reach, -reach, reach, reach), color);
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
        if (onEvent) {
            onEvent(event);
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
    qreal shadow = 0;   // how far beyond its geometry the view paints, and says so in paintBounds()
    qreal overdraw = 0; // how far beyond paintBounds() it paints without saying so
    QString surroundingText;
    int cursorPosition = 0;
    bool passThrough = false;
    int paintCount = 0;
    std::vector<bool> hovers;
    Events events;
    std::vector<QPointF> positions;
    Qt::InputMethodQueries queried;
    std::function<void(bool)> onHover;
    std::function<void(const QEvent&)> onEvent;
    std::function<void()> onDestroy;
};

// A view object that holds children, as a chrome container does: hitTest() hands out the child under the point, and
// the group keeps the child alive.
class GroupView final : public ViewObject {
  public:
    explicit GroupView(QRectF area, QString viewName = {}) : name(std::move(viewName)) { setGeometry(area); }

    TestView& addNested(QRectF area, QString childName = {}) { return add<TestView>(area, std::move(childName)); }
    GroupView& addNestedGroup(QRectF area) { return add<GroupView>(area); }
    void paint(QPainter& painter) override {
        for (const auto& child : nested) {
            painter.save();
            painter.setClipRect(child->paintBounds().toAlignedRect(), Qt::IntersectClip);
            child->paint(painter);
            painter.restore();
        }
    }
    ViewObject* hitTest(QPointF point) override {
        for (const auto& view : std::views::reverse(nested)) {
            if (ViewObject* hit = view->hitTest(point)) {
                return hit;
            }
        }
        return nullptr;
    }
    QString accessibleName() const override { return name; }
    QAccessible::Role accessibleRole() const override { return QAccessible::ToolBar; }

    QString name;
    std::vector<std::unique_ptr<ViewObject>> nested; // back to front; erase one to destroy it while the group stays

  private:
    template <typename View, typename... Args> View& add(Args&&... args) {
        auto& added = static_cast<View&>(*nested.emplace_back(std::make_unique<View>(std::forward<Args>(args)...)));
        if (!addChild(added)) {
            qFatal("the group's child was refused");
        }
        return added;
    }
};

TestView& addView(ViewRoot& root, QRectF area) {
    return static_cast<TestView&>(root.addView(std::make_unique<TestView>(area)));
}

GroupView& addGroup(ViewRoot& root, QRectF area, QString name = {}) {
    return static_cast<GroupView&>(root.addView(std::make_unique<GroupView>(area, std::move(name))));
}

// A view that paints 10 points beyond its geometry and declares that reach, as a view with a drop shadow would.
std::unique_ptr<TestView> makeShadowed(QRectF area) {
    auto view = std::make_unique<TestView>(area);
    view->shadow = 10;
    return view;
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

// The accessibility events seen while an ObservedAccessibility lives: what happened, to which interface, and where that
// interface stood in the tree at the time.
struct Change {
    QAccessible::Event type;
    QAccessibleInterface* node;
    QAccessibleInterface* parent;
    int index;
    bool operator==(const Change&) const = default;
};

std::vector<Change>& changes() {
    static std::vector<Change> recorded;
    return recorded;
}

std::function<void(QAccessibleEvent*)>& accessibilityCallback() {
    static std::function<void(QAccessibleEvent*)> callback;
    return callback;
}

void recordChange(QAccessibleEvent* event) {
    QAccessibleInterface* node = event->accessibleInterface();
    QAccessibleInterface* parent = node != nullptr ? node->parent() : nullptr;
    if (event->type() == QAccessible::ObjectDestroyed) {
        // Behaves like the Linux screen-reader bridge (atspiadaptor.cpp):
        // drops a null-parent ObjectDestroyed, and calls role() when parent is non-null.
        if (node == nullptr || parent == nullptr) {
            return;
        }
        [[maybe_unused]] const QAccessible::Role role = node->role();
    }
    changes().push_back({.type = event->type(),
                         .node = node,
                         .parent = parent,
                         .index = parent != nullptr ? parent->indexOfChild(node) : -1});
    if (auto& cb = accessibilityCallback()) {
        cb(event);
    }
}

Change change(QAccessible::Event type, QAccessibleInterface* node, QAccessibleInterface* parent, int index) {
    return {.type = type, .node = node, .parent = parent, .index = index};
}

// Switches accessibility on and records its events in changes() while it lives.
class ObservedAccessibility {
  public:
    ObservedAccessibility() : m_previous(QAccessible::installUpdateHandler(&recordChange)) {
        changes().clear();
        if (QPlatformIntegration* const pi = QGuiApplicationPrivate::platformIntegration()) {
            if (QPlatformAccessibility* const acc = pi->accessibility()) {
                acc->setActive(true);
            }
        }
        QAccessible::setActive(true);
    }
    ~ObservedAccessibility() {
        accessibilityCallback() = nullptr;
        if (QPlatformIntegration* const pi = QGuiApplicationPrivate::platformIntegration()) {
            if (QPlatformAccessibility* const acc = pi->accessibility()) {
                acc->setActive(false);
            }
        }
        QAccessible::setActive(false);
        QAccessible::installUpdateHandler(m_previous);
    }
    Q_DISABLE_COPY_MOVE(ObservedAccessibility)

  private:
    QAccessible::UpdateHandler m_previous;
};

// A view that overrides accessible() with its own interface.
class CustomAccessible final : public QAccessibleInterface {
  public:
    explicit CustomAccessible(ViewObject& view) : m_view(view) {}

    bool isValid() const override { return true; }
    QObject* object() const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    QAccessibleInterface* parent() const override { return m_view.accessibleParent(); }
    QAccessibleInterface* child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QString text(QAccessible::Text text) const override {
        return text == QAccessible::Name ? m_view.accessibleName() : QString{};
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { return m_view.screenRect(); }
    QAccessible::Role role() const override { return QAccessible::EditableText; }
    QAccessible::State state() const override { return {}; }

  private:
    ViewObject& m_view;
};

class CustomAccessibleView final : public ViewObject {
  public:
    explicit CustomAccessibleView(QRectF area) { setGeometry(area); }
    void paint(QPainter&) override {}
    QString accessibleName() const override { return QStringLiteral("custom"); }
    QAccessible::Role accessibleRole() const override { return QAccessible::EditableText; }
    QAccessibleInterface* accessible() override {
        if (m_id == 0) {
            m_id = QAccessible::registerAccessibleInterface(std::make_unique<CustomAccessible>(*this).release());
        }
        return QAccessible::accessibleInterface(m_id);
    }

  private:
    QAccessible::Id m_id = 0;
};

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
    void pressAfterALostReleaseStartsANewGesture();
    void aSecondButtonGoesToThePointerOwner();
    void removingThePointerOwnerCancelsTheGesture();
    void removingTheHolderOfAPointerOwnerCancelsTheGesture();
    void aViewRemovingItselfOnPressCancelsTheRestOfTheGesture();
    void aViewRemovingItselfOnReleaseLeavesTheNextGestureAlone();
    void aCancelledGestureLastsUntilTheLastButtonIsReleased();
    void removingAChildViewCancelsTheGestureItTook();
    void removingAnotherViewLeavesTheGestureAlone();
    void aMoveWithNoButtonHeldEndsACancelledGesture();
    void aPressAfterALostReleaseEndsACancelledGesture();
    void descendantDestroyedByItsHolderMidDragCancelsTheGesture();
    void descendantDestroyedWhileThePointerIsElsewhereCancelsTheGesture();
    void hoveredDescendantDestroyedByItsHolderIsNotNotified();
    void focusAndInputMethodOwnersDestroyedByTheirHolderReceiveNothing();
    void focusAndInputMethodOwnersRemovedReceiveNothing();
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
    void addedViewDamagesItsShadow();
    void movedViewDamagesItsOldAndNewShadow();
    void removedViewDamagesItsShadow();
    void viewsPaintOnlyWithinTheirDeclaredBounds();
    void cursorFollowsTheViewUnderThePointer();
    void keyboardModeIsReported();
    void viewObjectExposesAccessibleNameAndRole();
    void hostReachesTheAccessibleViewsAndLocatesThemOnTheScreen();
    void nestedViewHasItsHolderAsAccessibleParentAndTheScreenMapping();
    void nestedViewReportsItsDamageToTheRoot();
    void childRelationRefusesCyclesAndViewsTheRootHolds();
    void childAndParentForgetEachOtherWhenEitherIsDestroyed();
    void addingAChildViewToTheRootTakesItOutOfItsParent();
    void addingAndRemovingViewsAnnouncesTheTreeChange();
    void destroyingTheRootDetachesItsViews();
    void destroyingChildDirectlyDamagesOldBoundsAndAnnouncesDestruction();
    void childRemovalAndReparentingInsideNotificationHandlerIsSafe();
    void topLevelRemovalAndReparentingInsideNotificationHandlerIsSafe();
    void removingChildFromHolderWithoutRootRecordsNoEvents();
    void movingViewInsideRemovalHandlerClearsRemovingFlag();
    void viewOverridingAccessibleIsAnnouncedWhileDestroyed();
    void viewMovedAwayAndBackInsideRemovalHandlerEndsWithFinalAnnouncement();
    void topLevelViewMovedIntoGroupInsideRemovalHandlerClearsRemovingFlag();
    void removeViewDamagesLastPaintBounds();
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

// A real press goes down after a release that never arrived: the old gesture is over and the press belongs to the view
// under the pointer.
void ViewRootTest::pressAfterALostReleaseStartsANewGesture() {
    ViewRoot root;
    TestView& first = addView(root, {0, 0, 50, 50});
    TestView& second = addView(root, {60, 0, 50, 50});

    mouse(root, QEvent::MouseButtonPress, {10, 10});
    mouse(root, QEvent::MouseButtonPress, {70, 10}); // no release and no move in between

    QCOMPARE(first.events, Events{QEvent::MouseButtonPress});
    QCOMPARE(second.events, Events{QEvent::MouseButtonPress});
}

void ViewRootTest::aSecondButtonGoesToThePointerOwner() {
    ViewRoot root;
    TestView& first = addView(root, {0, 0, 50, 50});
    TestView& second = addView(root, {60, 0, 50, 50});

    mouse(root, QEvent::MouseButtonPress, {10, 10});
    const QPointF over(70, 10);
    root.dispatch(QMouseEvent(QEvent::MouseButtonPress, over, over, Qt::RightButton, Qt::LeftButton | Qt::RightButton,
                              Qt::NoModifier));

    QCOMPARE(first.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonPress}));
    QVERIFY(second.events.empty());
}

// The view that took the press is gone, so nothing may receive the rest of the gesture: the view underneath never saw
// the press and must not see a drag or a release (cf. macshot/UI/Toolbar/ToolbarButtonView.swift:268-295@b4d4f3a, where
// a drag and a release belong to the view that got the press).
void ViewRootTest::removingThePointerOwnerCancelsTheGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    const std::unique_ptr<ViewObject> removed = root.removeView(owner);
    drag(root, {50, 50});
    drag(root, {60, 60});
    mouse(root, QEvent::MouseButtonRelease, {60, 60});

    QCOMPARE(owner.events, Events{QEvent::MouseButtonPress});
    QVERIFY(behind.events.empty());

    mouse(root, QEvent::MouseButtonPress, {50, 50});
    mouse(root, QEvent::MouseButtonRelease, {50, 50});
    QCOMPARE(behind.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
}

void ViewRootTest::removingTheHolderOfAPointerOwnerCancelsTheGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    GroupView& holder = addGroup(root, {10, 10, 20, 20});
    TestView& child = holder.addNested({10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    const std::unique_ptr<ViewObject> removed = root.removeView(holder);
    drag(root, {70, 70});
    mouse(root, QEvent::MouseButtonRelease, {70, 70});

    QCOMPARE(child.events, Events{QEvent::MouseButtonPress});
    QCOMPARE(child.hovers, (std::vector<bool>{true, false}));
    QVERIFY(behind.events.empty());
}

void ViewRootTest::aViewRemovingItselfOnPressCancelsTheRestOfTheGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});
    std::unique_ptr<ViewObject> removed;
    owner.onEvent = [&](const QEvent& event) {
        if (event.type() == QEvent::MouseButtonPress && !removed) {
            removed = root.removeView(owner);
        }
    };

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    drag(root, {50, 50});
    mouse(root, QEvent::MouseButtonRelease, {50, 50});

    QVERIFY(removed != nullptr);
    QCOMPARE(owner.events, Events{QEvent::MouseButtonPress});
    QVERIFY(behind.events.empty());
}

// The release has already ended the gesture when the view goes, so the next gesture is not touched.
void ViewRootTest::aViewRemovingItselfOnReleaseLeavesTheNextGestureAlone() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});
    std::unique_ptr<ViewObject> removed;
    owner.onEvent = [&](const QEvent& event) {
        if (event.type() == QEvent::MouseButtonRelease && !removed) {
            removed = root.removeView(owner);
        }
    };

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    mouse(root, QEvent::MouseButtonRelease, {15, 15});
    mouse(root, QEvent::MouseButtonPress, {50, 50});
    mouse(root, QEvent::MouseButtonRelease, {50, 50});

    QVERIFY(removed != nullptr);
    QCOMPARE(owner.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
    QCOMPARE(behind.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
}

void ViewRootTest::aCancelledGestureLastsUntilTheLastButtonIsReleased() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});
    const QPointF at(15, 15);
    const auto button = [&](QEvent::Type type, Qt::MouseButton changed, Qt::MouseButtons held) {
        root.dispatch(QMouseEvent(type, at, at, changed, held, Qt::NoModifier));
    };

    button(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    button(QEvent::MouseButtonPress, Qt::RightButton, Qt::LeftButton | Qt::RightButton);
    const std::unique_ptr<ViewObject> removed = root.removeView(owner);
    button(QEvent::MouseButtonRelease, Qt::RightButton, Qt::LeftButton); // the left button is still down
    drag(root, {50, 50});
    button(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);

    QVERIFY(behind.events.empty());
    mouse(root, QEvent::MouseButtonPress, {50, 50});
    QCOMPARE(behind.events, Events{QEvent::MouseButtonPress});
}

// The same goes for a view that a holder takes out of its tree without destroying it, and for one deeper down.
void ViewRootTest::removingAChildViewCancelsTheGestureItTook() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    GroupView& outer = addGroup(root, {10, 10, 40, 40});
    GroupView& inner = outer.addNestedGroup({10, 10, 20, 20});
    TestView& button = inner.addNested({10, 10, 10, 10});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    QVERIFY(outer.removeChild(inner));
    drag(root, {70, 70});
    mouse(root, QEvent::MouseButtonRelease, {70, 70});

    QCOMPARE(button.events, Events{QEvent::MouseButtonPress});
    QVERIFY(behind.events.empty());
    mouse(root, QEvent::MouseButtonPress, {70, 70});
    QCOMPARE(behind.events, Events{QEvent::MouseButtonPress});
}

void ViewRootTest::removingAnotherViewLeavesTheGestureAlone() {
    ViewRoot root;
    TestView& button = addView(root, {0, 0, 50, 50});
    TestView& other = addView(root, {60, 0, 50, 50});
    GroupView& holder = addGroup(root, {0, 60, 50, 40});
    TestView& child = holder.addNested({0, 60, 10, 10});

    mouse(root, QEvent::MouseButtonPress, {10, 10});
    const std::unique_ptr<ViewObject> removedView = root.removeView(other);
    const std::unique_ptr<ViewObject> removedHolder = root.removeView(holder);
    drag(root, {70, 10});
    mouse(root, QEvent::MouseButtonRelease, {70, 10});

    QCOMPARE(button.events, (Events{QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}));
    QVERIFY(child.events.empty());
}

// The release of a cancelled gesture can be lost like any other: a move with no button held ends it.
void ViewRootTest::aMoveWithNoButtonHeldEndsACancelledGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    const std::unique_ptr<ViewObject> removed = root.removeView(owner);
    mouse(root, QEvent::MouseMove, {50, 50});

    QCOMPARE(behind.events, Events{QEvent::MouseMove});
}

void ViewRootTest::aPressAfterALostReleaseEndsACancelledGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    TestView& owner = addView(root, {10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    const std::unique_ptr<ViewObject> removed = root.removeView(owner);
    mouse(root, QEvent::MouseButtonPress, {50, 50}); // no release and no move in between

    QCOMPARE(behind.events, Events{QEvent::MouseButtonPress});
}

// hitTest() may hand out a view the holder owns and can destroy at any time. The root must not call a view that is
// gone: the gesture is cancelled, and the next one reaches whatever is under the pointer then.
void ViewRootTest::descendantDestroyedByItsHolderMidDragCancelsTheGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    GroupView& holder = addGroup(root, {10, 10, 40, 40});
    holder.addNested({10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    holder.nested.clear();
    drag(root, {70, 70});
    mouse(root, QEvent::MouseButtonRelease, {70, 70});

    QVERIFY(behind.events.empty());
    mouse(root, QEvent::MouseButtonPress, {15, 15});
    mouse(root, QEvent::MouseButtonRelease, {15, 15});
    QCOMPARE(behind.events, (Events{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
}

// The pointer is over another view when the holder destroys the owner, so only the owner itself refers to it.
void ViewRootTest::descendantDestroyedWhileThePointerIsElsewhereCancelsTheGesture() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    GroupView& holder = addGroup(root, {10, 10, 40, 40});
    holder.addNested({10, 10, 20, 20});

    mouse(root, QEvent::MouseButtonPress, {15, 15});
    drag(root, {80, 80});
    holder.nested.clear();
    drag(root, {85, 85});
    mouse(root, QEvent::MouseButtonRelease, {85, 85});

    QVERIFY(behind.events.empty());
    mouse(root, QEvent::MouseButtonPress, {85, 85});
    QCOMPARE(behind.events, Events{QEvent::MouseButtonPress});
}

void ViewRootTest::hoveredDescendantDestroyedByItsHolderIsNotNotified() {
    ViewRoot root;
    TestView& behind = addView(root, {0, 0, 100, 100});
    GroupView& holder = addGroup(root, {10, 10, 40, 40});
    TestView& child = holder.addNested({10, 10, 20, 20});
    mouse(root, QEvent::MouseMove, {15, 15});
    QCOMPARE(child.hovers, std::vector<bool>{true});

    holder.nested.clear();
    mouse(root, QEvent::MouseMove, {16, 16});

    QCOMPARE(behind.hovers, std::vector<bool>{true});
    mouse(root, QEvent::MouseMove, {500, 500});
    QCOMPARE(behind.hovers, (std::vector<bool>{true, false}));
}

void ViewRootTest::focusAndInputMethodOwnersDestroyedByTheirHolderReceiveNothing() {
    ViewRoot root;
    GroupView& holder = addGroup(root, {10, 10, 40, 40});
    TestView& child = holder.addNested({10, 10, 20, 20});
    root.setFocusOwner(&child);
    root.setInputMethodOwner(&child);

    holder.nested.clear();
    root.dispatch(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
    root.dispatch(QInputMethodEvent(u"a"_s, {}));
    QInputMethodQueryEvent query(Qt::ImEnabled);
    root.inputMethodQuery(query);

    QCOMPARE(query.value(Qt::ImEnabled), QVariant(false));
}

void ViewRootTest::focusAndInputMethodOwnersRemovedReceiveNothing() {
    ViewRoot root;
    GroupView& holder = addGroup(root, {10, 10, 60, 60});
    GroupView& subGroup = holder.addNestedGroup({10, 10, 40, 40});
    TestView& child = subGroup.addNested({10, 10, 20, 20});
    root.setFocusOwner(&child);
    root.setInputMethodOwner(&child);

    // Removing an ancestor via removeChild clears focus and input method owners.
    holder.removeChild(subGroup);
    root.dispatch(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
    root.dispatch(QInputMethodEvent(u"a"_s, {}));
    QInputMethodQueryEvent query(Qt::ImEnabled);
    root.inputMethodQuery(query);

    QCOMPARE(child.events, Events{});
    QCOMPARE(query.value(Qt::ImEnabled), QVariant(false));

    // Removing an ancestor via removeView clears focus and input method owners.
    GroupView& holder2 = addGroup(root, {10, 10, 60, 60});
    GroupView& subGroup2 = holder2.addNestedGroup({10, 10, 40, 40});
    TestView& child2 = subGroup2.addNested({10, 10, 20, 20});
    root.setFocusOwner(&child2);
    root.setInputMethodOwner(&child2);

    const std::unique_ptr<ViewObject> removed = root.removeView(holder2);
    root.dispatch(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
    root.dispatch(QInputMethodEvent(u"a"_s, {}));
    QInputMethodQueryEvent query2(Qt::ImEnabled);
    root.inputMethodQuery(query2);

    QCOMPARE(child2.events, Events{});
    QCOMPARE(query2.value(Qt::ImEnabled), QVariant(false));
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
    TestView& view = static_cast<TestView&>(root.addView(makeShadowed({40, 40, 20, 20})));
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(32, 50), QColor(Qt::red));

    view.update({30, 40, 10, 20});
    (void)root.takeDamage();

    QCOMPARE(chrome.pixelColor(32, 50), QColor(Qt::red));
}

// What a view paints beyond its geometry has to appear when the view does, move with it and go when it goes: the damage
// of these changes is the view's paintBounds(), not just its geometry.
void ViewRootTest::addedViewDamagesItsShadow() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    (void)root.takeDamage();

    root.addView(makeShadowed({40, 40, 20, 20}));

    QCOMPARE(root.takeDamage(), QRegion(30, 30, 40, 40));
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(32, 50), QColor(Qt::red));
    QCOMPARE(chrome.pixelColor(50, 50), QColor(Qt::red));
}

void ViewRootTest::movedViewDamagesItsOldAndNewShadow() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    TestView& view = static_cast<TestView&>(root.addView(makeShadowed({40, 40, 20, 20})));
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(65, 65), QColor(Qt::red));

    view.setGeometry({10, 10, 20, 20});
    (void)root.takeDamage();

    QCOMPARE(chrome.pixelColor(65, 65).alpha(), 0); // the old shadow is gone
    QCOMPARE(chrome.pixelColor(45, 45).alpha(), 0);
    QCOMPARE(chrome.pixelColor(5, 5), QColor(Qt::red)); // the new one is there, outside the new geometry
    QCOMPARE(chrome.pixelColor(20, 20), QColor(Qt::red));
}

void ViewRootTest::removedViewDamagesItsShadow() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    TestView& view = static_cast<TestView&>(root.addView(makeShadowed({40, 40, 20, 20})));
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(65, 65), QColor(Qt::red));

    const std::unique_ptr<ViewObject> removed = root.removeView(view);

    QCOMPARE(root.takeDamage(), QRegion(30, 30, 40, 40));
    QCOMPARE(chrome.pixelColor(65, 65).alpha(), 0);
    QCOMPARE(chrome.pixelColor(45, 45).alpha(), 0);
}

// The root clips each view to its declared bounds, so a view that paints more than it declared cannot leave pixels that
// no damage will ever clear.
void ViewRootTest::viewsPaintOnlyWithinTheirDeclaredBounds() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    std::unique_ptr<TestView> view = makeShadowed({40, 40, 20, 20});
    view->overdraw = 5;
    root.addView(std::move(view));

    (void)root.takeDamage();

    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(32, 50), QColor(Qt::red)); // declared: the shadow
    QCOMPARE(chrome.pixelColor(27, 50).alpha(), 0);       // not declared: five points further out
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

// A view that hitTest() hands out is a child of its holder: the holder is its accessible parent, the screen mapping of
// the host reaches it, and the holder's interface lists it (https://doc.qt.io/qt-6.8/qaccessibleinterface.html).
void ViewRootTest::nestedViewHasItsHolderAsAccessibleParentAndTheScreenMapping() {
    ViewRoot root;
    root.resize({200, 100}, 1.0);
    GroupView& group = addGroup(root, {10, 10, 100, 40}, u"Toolbar"_s);
    TestView& save = group.addNested({20, 15, 20, 20}, u"Save"_s);
    TestView& copy = group.addNested({50, 15, 20, 20}, u"Copy"_s);
    HostAccessible host(root);
    QPoint origin(1920, 100);
    static_cast<SurfaceContent&>(root).attachAccessible(&host, [&origin](QPoint point) { return point + origin; });

    QAccessibleInterface* content = host.child(0);
    QAccessibleInterface* toolbar = content != nullptr ? content->child(0) : nullptr;
    if (toolbar == nullptr) {
        QFAIL("the content does not list the group");
    }
    QCOMPARE(toolbar->text(QAccessible::Name), u"Toolbar"_s);
    QCOMPARE(toolbar->childCount(), 2);
    QAccessibleInterface* saveNode = toolbar->child(0);
    QAccessibleInterface* copyNode = toolbar->child(1);
    if (saveNode == nullptr || copyNode == nullptr) {
        QFAIL("the group does not list its children");
    }
    QCOMPARE(saveNode->text(QAccessible::Name), u"Save"_s);
    QCOMPARE(copyNode->text(QAccessible::Name), u"Copy"_s);
    QVERIFY(saveNode->parent() == toolbar);
    QVERIFY(copyNode->parent() == toolbar);
    QVERIFY(toolbar->child(2) == nullptr);
    QCOMPARE(toolbar->indexOfChild(saveNode), 0);
    QCOMPARE(toolbar->indexOfChild(copyNode), 1);
    QCOMPARE(toolbar->indexOfChild(content), -1);
    QCOMPARE(saveNode->rect(), QRect(1940, 115, 20, 20));
    QVERIFY(toolbar->childAt(1945, 120) == saveNode);
    QVERIFY(toolbar->childAt(1975, 120) == copyNode);
    QVERIFY(toolbar->childAt(1935, 112) == nullptr); // inside the group, on none of its children
    QVERIFY(content->childAt(1945, 120) == toolbar);

    origin = {0, 50};
    QCOMPARE(copyNode->rect(), QRect(50, 65, 20, 20));
    QCOMPARE(save.screenRect(), QRect(20, 65, 20, 20));

    // It is the view the root's hit test hands out, so it gets the events.
    mouse(root, QEvent::MouseButtonPress, {25, 20});
    QCOMPARE(save.events, Events{QEvent::MouseButtonPress});
    QVERIFY(copy.events.empty());
}

void ViewRootTest::nestedViewReportsItsDamageToTheRoot() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    GroupView& group = addGroup(root, {10, 10, 60, 60});
    (void)root.takeDamage();

    TestView& nested = group.addNested({20, 20, 10, 10});
    QCOMPARE(root.takeDamage(), QRegion(20, 20, 10, 10));

    nested.setGeometry({40, 40, 10, 10});
    QCOMPARE(root.takeDamage(), QRegion(20, 20, 10, 10).united(QRegion(40, 40, 10, 10)));

    nested.update({41, 41, 2, 2});
    QCOMPARE(root.takeDamage(), QRegion(41, 41, 2, 2));

    QVERIFY(group.removeChild(nested));
    QCOMPARE(root.takeDamage(), QRegion(40, 40, 10, 10));
    nested.update({41, 41, 2, 2}); // no longer part of the tree
    QVERIFY(root.takeDamage().isEmpty());
}

void ViewRootTest::childRelationRefusesCyclesAndViewsTheRootHolds() {
    ViewRoot root;
    TestView& held = addView(root, {0, 0, 10, 10});
    TestView top({0, 0, 10, 10});
    TestView middle({0, 0, 10, 10});
    TestView bottom({0, 0, 10, 10});
    QVERIFY(top.addChild(middle));
    QVERIFY(middle.addChild(bottom));

    QVERIFY(!top.addChild(top));    // itself
    QVERIFY(!bottom.addChild(top)); // an ancestor: the parent chain would never end
    QVERIFY(!middle.addChild(top));
    QVERIFY(!top.addChild(held));  // a view the root holds is a top-level view
    QVERIFY(top.addChild(middle)); // already its child: nothing changes
    QCOMPARE(top.children(), (std::vector<ViewObject*>{&middle}));

    QVERIFY(bottom.removeChild(top) == false); // not its child
    QVERIFY(top.removeChild(middle));
    QVERIFY(top.children().empty());
    QVERIFY(!top.removeChild(middle));
    QCOMPARE(middle.children(), (std::vector<ViewObject*>{&bottom}));

    TestView other({0, 0, 10, 10});
    QVERIFY(other.addChild(bottom)); // a child moves to its new parent
    QVERIFY(middle.children().empty());
    QCOMPARE(other.children(), (std::vector<ViewObject*>{&bottom}));
}

void ViewRootTest::childAndParentForgetEachOtherWhenEitherIsDestroyed() {
    auto parent = std::make_unique<TestView>(QRectF(0, 0, 10, 10));
    TestView firstChild({0, 0, 5, 5});
    auto secondChild = std::make_unique<TestView>(QRectF(5, 5, 5, 5));
    QVERIFY(parent->addChild(firstChild));
    QVERIFY(parent->addChild(*secondChild));

    secondChild.reset();
    QCOMPARE(parent->children(), (std::vector<ViewObject*>{&firstChild}));

    parent.reset();
    QVERIFY(firstChild.accessibleParent() == nullptr);
    firstChild.update({0, 0, 5, 5}); // reaches nothing, and nothing dangles
    QCOMPARE(firstChild.screenRect(), QRect(0, 0, 5, 5));
}

void ViewRootTest::addingAChildViewToTheRootTakesItOutOfItsParent() {
    ViewRoot root;
    TestView parent({0, 0, 10, 10});
    auto child = std::make_unique<TestView>(QRectF(0, 0, 5, 5));
    QVERIFY(parent.addChild(*child));

    ViewObject& held = root.addView(std::move(child));

    QVERIFY(parent.children().empty());
    QVERIFY(held.accessibleParent() == root.accessible());
}

// Assistive technology learns of a view added to or removed from the tree through the accessibility events of Qt
// (https://doc.qt.io/qt-6.8/qaccessible.html): created once the view is in the tree, destroyed while it still is.
void ViewRootTest::addingAndRemovingViewsAnnouncesTheTreeChange() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    GroupView& group = addGroup(root, {10, 10, 60, 60});
    TestView& nested = group.addNested({20, 20, 10, 10});
    QAccessibleInterface* groupNode = group.accessible();
    QAccessibleInterface* nestedNode = nested.accessible();
    QAccessibleInterface* rootNode = root.accessible();
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectCreated, groupNode, rootNode, 0),
                                             change(QAccessible::ObjectCreated, nestedNode, groupNode, 0)}));

    changes().clear();
    QVERIFY(group.removeChild(nested));
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, nestedNode, groupNode, 0)}));

    // 1. Remove, re-add, remove again: announces on both removals.
    QVERIFY(group.addChild(nested));
    changes().clear();
    QVERIFY(group.removeChild(nested));
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, nestedNode, groupNode, 0)}));

    // 2. Move then remove: moving a child to another holder and removing it announces under the new holder.
    GroupView& group2 = addGroup(root, {80, 80, 40, 40});
    TestView& moving = group.addNested({5, 5, 10, 10});
    QAccessibleInterface* movingNode = moving.accessible();
    QAccessibleInterface* group2Node = group2.accessible();
    QVERIFY(group2.addChild(moving));
    changes().clear();
    QVERIFY(group2.removeChild(moving));
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, movingNode, group2Node, 0)}));

    changes().clear();
    const std::unique_ptr<ViewObject> removed = root.removeView(group);
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, groupNode, rootNode, 0)}));
    (void)root.removeView(group2);

    // 3. removeView, addView, removeView: top-level view announces on both removals.
    TestView& top = addView(root, {0, 0, 10, 10});
    QAccessibleInterface* topNode = top.accessible();
    std::unique_ptr<ViewObject> removedTop = root.removeView(top);
    ViewObject& readdedTop = root.addView(std::move(removedTop));
    changes().clear();
    const std::unique_ptr<ViewObject> removedAgain = root.removeView(readdedTop);
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, topNode, rootNode, 0)}));
}

// A view can call update() from its destructor; by then the root's members are gone, so the root lets go of its views
// first.
void ViewRootTest::destroyingTheRootDetachesItsViews() {
    const ObservedAccessibility observed;
    auto root = std::make_unique<ViewRoot>();
    root->resize({100, 100}, 1.0);
    GroupView& group = addGroup(*root, {10, 10, 60, 60});
    TestView& nested = group.addNested({20, 20, 10, 10});
    TestView& top = addView(*root, {0, 0, 5, 5});
    (void)top.accessible();
    nested.onDestroy = [&nested] { nested.update({20, 20, 10, 10}); };
    top.onDestroy = [&top] { top.update({0, 0, 5, 5}); };

    // Raising a view (removeView then addView) leaves accessibleParent() null once detached,
    // so destroying the root does not trigger a use-after-free on the former accessible parent.
    TestView& raised = addView(*root, {50, 50, 20, 20});
    (void)raised.accessible();
    std::unique_ptr<ViewObject> detached = root->removeView(raised);
    root->addView(std::move(detached));

    root.reset();
}

void ViewRootTest::destroyingChildDirectlyDamagesOldBoundsAndAnnouncesDestruction() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    GroupView& group = addGroup(root, {0, 0, 100, 100});
    TestView& child = group.addNested({10, 10, 20, 20});
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(chrome.pixelColor(15, 15), QColor(Qt::red));

    const ObservedAccessibility observed;
    changes().clear();
    QAccessibleInterface* childNode = child.accessible();
    QAccessibleInterface* groupNode = group.accessible();

    bool bridgeChecked = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == childNode) {
            QAccessibleInterface* iface = event->accessibleInterface();
            QVERIFY(iface->parent() != nullptr);
            QCOMPARE(iface->role(), QAccessible::PushButton);
            bridgeChecked = true;
        }
    };

    group.nested.clear();
    accessibilityCallback() = nullptr;

    QVERIFY(bridgeChecked);
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, childNode, groupNode, 0)}));
    QCOMPARE(root.takeDamage(), QRegion(10, 10, 20, 20));
    QCOMPARE(chrome.pixelColor(15, 15).alpha(), 0);

    // Updating reach without moving updates the saved paint bounds so direct destruction damages the full area.
    TestView& childWithShadow = group.addNested({40, 40, 20, 20});
    (void)root.takeDamage();
    childWithShadow.shadow = 10;
    childWithShadow.update(childWithShadow.paintBounds());
    (void)root.takeDamage();
    group.nested.clear();
    QCOMPARE(root.takeDamage(), QRegion(30, 30, 40, 40));
}

void ViewRootTest::childRemovalAndReparentingInsideNotificationHandlerIsSafe() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    GroupView& group1 = addGroup(root, {0, 0, 50, 50});
    GroupView& group2 = addGroup(root, {50, 50, 50, 50});
    TestView& child1 = group1.addNested({10, 10, 20, 20});
    (void)root.takeDamage();
    root.setFocusOwner(&child1);
    root.setInputMethodOwner(&child1);
    QAccessibleInterface* child1Node = child1.accessible();

    bool handled1 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled1 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == child1Node) {
            handled1 = true;
            QVERIFY(group2.addChild(child1));
            group2.nested.push_back(std::move(group1.nested.front()));
            group1.nested.erase(group1.nested.begin());
        }
    };
    QVERIFY(group1.removeChild(child1));
    accessibilityCallback() = nullptr;
    QVERIFY(group1.children().empty());
    QCOMPARE(group2.children(), (std::vector<ViewObject*>{&child1}));
    QCOMPARE(child1.accessibleParent(), group2.accessible());
    root.dispatch(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
    QCOMPARE(child1.events, Events{});
    QInputMethodQueryEvent query(Qt::ImEnabled);
    root.inputMethodQuery(query);
    QCOMPARE(query.value(Qt::ImEnabled), QVariant(false));
    QCOMPARE(root.takeDamage(), QRegion(10, 10, 20, 20));

    changes().clear();
    TestView& child2 = group1.addNested({10, 10, 20, 20});
    QAccessibleInterface* child2Node = child2.accessible();
    QAccessibleInterface* group1Node = group1.accessible();
    changes().clear();
    bool handled2 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled2 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == child2Node) {
            handled2 = true;
            group1.nested.clear();
        }
    };
    QVERIFY(group1.removeChild(child2));
    accessibilityCallback() = nullptr;
    QVERIFY(group1.children().empty());
    QCOMPARE(changes(), (std::vector<Change>{change(QAccessible::ObjectDestroyed, child2Node, group1Node, 0)}));

    TestView& child3 = group1.addNested({10, 10, 20, 20});
    QAccessibleInterface* child3Node = child3.accessible();
    bool reentrantResult = true;
    bool handled3 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled3 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == child3Node) {
            handled3 = true;
            reentrantResult = group1.removeChild(child3);
        }
    };
    QVERIFY(group1.removeChild(child3));
    accessibilityCallback() = nullptr;
    QVERIFY(!reentrantResult);
    QVERIFY(group1.children().empty());

    // Move-path: addChild() triggers removeChild() on the previous parent.
    // If the handler destroys the moving child during the removal notification, addChild returns false.
    GroupView& group3 = addGroup(root, {0, 0, 40, 40});
    GroupView& group4 = addGroup(root, {50, 0, 40, 40});
    TestView& moving1 = group3.addNested({5, 5, 10, 10});
    QAccessibleInterface* moving1Node = moving1.accessible();
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == moving1Node) {
            group3.nested.clear();
        }
    };
    QVERIFY(!group4.addChild(moving1));
    accessibilityCallback() = nullptr;
    QVERIFY(group3.children().empty());
    QVERIFY(group4.children().empty());

    // Move-path: if handler reparents the moving child during removal notification, addChild returns false.
    GroupView& group5 = addGroup(root, {0, 50, 40, 40});
    TestView& moving2 = group3.addNested({5, 5, 10, 10});
    QAccessibleInterface* moving2Node = moving2.accessible();
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == moving2Node) {
            QVERIFY(group5.addChild(moving2));
        }
    };
    QVERIFY(!group4.addChild(moving2));
    accessibilityCallback() = nullptr;
    QVERIFY(group3.children().empty());
    QVERIFY(group4.children().empty());
    QCOMPARE(group5.children(), (std::vector<ViewObject*>{&moving2}));
}

void ViewRootTest::topLevelRemovalAndReparentingInsideNotificationHandlerIsSafe() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    TestView& top1 = addView(root, {0, 0, 20, 20});
    QAccessibleInterface* top1Node = top1.accessible();
    std::unique_ptr<ViewObject> reentrantRemoved;
    bool handledTop1 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handledTop1 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == top1Node) {
            handledTop1 = true;
            reentrantRemoved = root.removeView(top1);
        }
    };
    std::unique_ptr<ViewObject> removed1 = root.removeView(top1);
    accessibilityCallback() = nullptr;
    QVERIFY(removed1.get() == &top1);
    QVERIFY(reentrantRemoved == nullptr);

    TestView& top2 = addView(root, {0, 0, 20, 20});
    TestView& top3 = addView(root, {30, 30, 20, 20});
    QAccessibleInterface* top2Node = top2.accessible();
    std::unique_ptr<ViewObject> siblingRemoved;
    bool handledTop2 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handledTop2 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == top2Node) {
            handledTop2 = true;
            siblingRemoved = root.removeView(top3);
        }
    };
    std::unique_ptr<ViewObject> removed2 = root.removeView(top2);
    accessibilityCallback() = nullptr;
    QVERIFY(removed2.get() == &top2);
    QVERIFY(siblingRemoved.get() == &top3);

    TestView& top4 = addView(root, {0, 0, 20, 20});
    GroupView& group = addGroup(root, {0, 0, 50, 50});
    QAccessibleInterface* top4Node = top4.accessible();
    bool reparented = false;
    bool handledTop4 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handledTop4 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == top4Node) {
            handledTop4 = true;
            reparented = group.addChild(top4);
        }
    };
    std::unique_ptr<ViewObject> removed4 = root.removeView(top4);
    accessibilityCallback() = nullptr;
    QVERIFY(reparented);
    QCOMPARE(top4.accessibleParent(), group.accessible());
    QCOMPARE(group.children(), (std::vector<ViewObject*>{&top4}));

    // Calling addView on a child whose removal is in progress terminates instead of looping infinitely.
    GroupView& groupForRemoval = addGroup(root, {0, 0, 50, 50});
    auto movingView = std::make_unique<TestView>(QRectF{5, 5, 10, 10});
    TestView* const movingViewPtr = movingView.get();
    QVERIFY(groupForRemoval.addChild(*movingView));
    QAccessibleInterface* movingViewNode = movingView->accessible();
    bool addViewTerminated = false;
    bool inCallback = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (inCallback) {
            return;
        }
        if (event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == movingViewNode) {
            inCallback = true;
            root.addView(std::move(movingView));
            addViewTerminated = true;
            std::unique_ptr<ViewObject> retrieved = root.removeView(*movingViewPtr);
            QVERIFY(retrieved != nullptr);
            QCOMPARE(retrieved.get(), movingViewPtr);
            root.addView(std::move(retrieved));
            QVERIFY(groupForRemoval.children().empty());
            QCOMPARE(movingViewPtr->accessibleParent(), root.accessible());
            inCallback = false;
        }
    };
    (void)groupForRemoval.removeChild(*movingViewPtr);
    accessibilityCallback() = nullptr;
    QVERIFY(addViewTerminated);
    QVERIFY(groupForRemoval.children().empty());
    QCOMPARE(movingViewPtr->accessibleParent(), root.accessible());
}

void ViewRootTest::removingChildFromHolderWithoutRootRecordsNoEvents() {
    const ObservedAccessibility observed;
    GroupView holder({0, 0, 100, 100});
    TestView& child = holder.addNested({10, 10, 20, 20});
    changes().clear();

    QVERIFY(holder.removeChild(child));
    QVERIFY(changes().empty());
}

void ViewRootTest::movingViewInsideRemovalHandlerClearsRemovingFlag() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    // A view moved inside its own removal handler and then destroyed announces its destruction.
    GroupView& group1 = addGroup(root, {0, 0, 50, 50});
    GroupView& group2 = addGroup(root, {50, 0, 50, 50});
    auto moving1 = std::make_unique<TestView>(QRectF{10, 10, 20, 20});
    TestView* const moving1Ptr = moving1.get();
    QVERIFY(group1.addChild(*moving1));
    QAccessibleInterface* moving1Node = moving1->accessible();
    QAccessibleInterface* group1Node = group1.accessible();
    QAccessibleInterface* group2Node = group2.accessible();

    changes().clear();
    bool handled1 = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled1 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == moving1Node) {
            handled1 = true;
            QVERIFY(group2.addChild(*moving1Ptr));
            moving1.reset();
        }
    };
    QVERIFY(group1.removeChild(*moving1Ptr));
    accessibilityCallback() = nullptr;
    QVERIFY(handled1);
    QCOMPARE(changes(), (std::vector<Change>{
                            change(QAccessible::ObjectDestroyed, moving1Node, group1Node, 0),
                            change(QAccessible::ObjectCreated, moving1Node, group2Node, 0),
                            change(QAccessible::ObjectDestroyed, moving1Node, group2Node, 0),
                        }));

    // removeChild on a view moved inside its removal handler succeeds inside the handler.
    GroupView& group3 = addGroup(root, {0, 50, 50, 50});
    GroupView& group4 = addGroup(root, {50, 50, 50, 50});
    TestView& moving2 = group3.addNested({10, 10, 20, 20});
    QAccessibleInterface* moving2Node = moving2.accessible();

    bool handled2 = false;
    bool removeSucceeded = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled2 && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == moving2Node) {
            handled2 = true;
            QVERIFY(group4.addChild(moving2));
            removeSucceeded = group4.removeChild(moving2);
        }
    };
    QVERIFY(group3.removeChild(moving2));
    accessibilityCallback() = nullptr;
    QVERIFY(handled2);
    QVERIFY(removeSucceeded);
    QVERIFY(group3.children().empty());
    QVERIFY(group4.children().empty());
}

void ViewRootTest::viewOverridingAccessibleIsAnnouncedWhileDestroyed() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    auto custom = std::make_unique<CustomAccessibleView>(QRectF{10, 10, 20, 20});
    CustomAccessibleView* const customPtr = custom.get();
    QAccessibleInterface* const customNode = customPtr->accessible();
    QVERIFY(customNode != nullptr);

    GroupView& group = addGroup(root, {0, 0, 50, 50});
    QVERIFY(group.addChild(*custom));
    QAccessibleInterface* const groupNode = group.accessible();

    changes().clear();
    custom.reset();

    QCOMPARE(changes(), (std::vector<Change>{
                            change(QAccessible::ObjectDestroyed, customNode, groupNode, 0),
                        }));
}

void ViewRootTest::viewMovedAwayAndBackInsideRemovalHandlerEndsWithFinalAnnouncement() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    GroupView& group1 = addGroup(root, {0, 0, 50, 50});
    GroupView& group2 = addGroup(root, {50, 0, 50, 50});
    TestView& child = group1.addNested({10, 10, 20, 20});
    QAccessibleInterface* const childNode = child.accessible();
    QAccessibleInterface* const group1Node = group1.accessible();

    bool handled = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == childNode) {
            handled = true;
            QVERIFY(group2.addChild(child));
            QVERIFY(group1.addChild(child));
        }
    };
    QVERIFY(group1.removeChild(child));
    accessibilityCallback() = nullptr;
    QVERIFY(handled);
    QCOMPARE(group1.children(), (std::vector<ViewObject*>{&child}));
    QCOMPARE(child.accessibleParent(), group1Node);

    changes().clear();
    QVERIFY(group1.removeChild(child));
    QCOMPARE(changes(), (std::vector<Change>{
                            change(QAccessible::ObjectDestroyed, childNode, group1Node, 0),
                        }));
}

void ViewRootTest::topLevelViewMovedIntoGroupInsideRemovalHandlerClearsRemovingFlag() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    const ObservedAccessibility observed;

    auto top = std::make_unique<TestView>(QRectF{0, 0, 20, 20});
    TestView* const topPtr = top.get();
    root.addView(std::move(top));
    GroupView& group = addGroup(root, {50, 50, 50, 50});
    QAccessibleInterface* const topNode = topPtr->accessible();

    bool handled = false;
    bool removeChildSucceeded = false;
    accessibilityCallback() = [&](QAccessibleEvent* event) {
        if (!handled && event->type() == QAccessible::ObjectDestroyed && event->accessibleInterface() == topNode) {
            handled = true;
            QVERIFY(group.addChild(*topPtr));
            removeChildSucceeded = group.removeChild(*topPtr);
        }
    };
    (void)root.removeView(*topPtr);
    accessibilityCallback() = nullptr;
    QVERIFY(handled);
    QVERIFY(removeChildSucceeded);
}

void ViewRootTest::removeViewDamagesLastPaintBounds() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);

    auto view = std::make_unique<TestView>(QRectF{40, 40, 20, 20});
    TestView* const viewPtr = view.get();
    viewPtr->shadow = 10;
    root.addView(std::move(view));
    (void)root.takeDamage();

    viewPtr->shadow = 0;
    (void)root.removeView(*viewPtr);

    QCOMPARE(root.takeDamage(), QRegion(30, 30, 40, 40));
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
