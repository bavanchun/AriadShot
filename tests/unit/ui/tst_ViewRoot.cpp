// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "platform/SurfaceHost.h"
#include "ui/ViewObject.h"
#include "ui/ViewRoot.h"

#include <QAccessible>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTest>
#include <QWheelEvent>

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
        painter.fillRect(geometry(), color);
    }
    ViewObject* hitTest(QPointF point) override { return passThrough ? nullptr : ViewObject::hitTest(point); }
    void hoverChanged(bool hovered) override { hovers.push_back(hovered); }
    void handleEvent(const QEvent& event) override { events.push_back(event.type()); }
    std::optional<Qt::CursorShape> cursorAt(QPointF) const override { return cursor; }
    QString accessibleName() const override { return name; }
    QAccessible::Role accessibleRole() const override { return role; }

    QString name;
    QAccessible::Role role = QAccessible::PushButton;
    std::optional<Qt::CursorShape> cursor = Qt::CrossCursor;
    QColor color = Qt::red;
    bool passThrough = false;
    int paintCount = 0;
    std::vector<bool> hovers;
    Events events;
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
    void passThroughViewLetsTheViewBehindReceiveTheEvent();
    void wheelAndTabletEventsRouteLikeMouseEvents();
    void hoverFollowsThePointer();
    void keyAndFocusEventsGoToTheFocusOwner();
    void inputMethodEventsGoToTheInputMethodOwner();
    void removedViewIsForgotten();
    void updateAccumulatesDamageUntilTaken();
    void layersAreCanonicalLayersThenChrome();
    void chromeLayerIsRepaintedOverTheDamageOnly();
    void cursorFollowsTheViewUnderThePointer();
    void keyboardModeIsReported();
    void viewObjectExposesAccessibleNameAndRole();
    void hostPresentsContentThroughThePlatformInterfaces();
};

void ViewRootTest::pointerEventsGoToTheFrontmostViewUnderThePointer() {
    ViewRoot root;
    TestView& back = addView(root, {0, 0, 100, 100});
    TestView& front = addView(root, {50, 50, 100, 100});

    mouse(root, QEvent::MouseButtonPress, {75, 75});
    mouse(root, QEvent::MouseButtonRelease, {10, 10});
    mouse(root, QEvent::MouseMove, {500, 500});

    QCOMPARE(front.events, Events{QEvent::MouseButtonPress});
    QCOMPARE(back.events, Events{QEvent::MouseButtonRelease});
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
        root.dispatch(
            QTabletEvent(type, &stylus, at, at, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton));
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

void ViewRootTest::chromeLayerIsRepaintedOverTheDamageOnly() {
    ViewRoot root;
    root.resize({100, 100}, 1.0);
    TestView& first = addView(root, {10, 10, 20, 20});
    (void)root.takeDamage();
    const QImage& chrome = *root.presentationLayers().back();
    QCOMPARE(first.paintCount, 1);
    QCOMPARE(chrome.pixelColor(15, 15), QColor(Qt::red));
    QCOMPARE(chrome.pixelColor(50, 50).alpha(), 0);

    TestView& second = addView(root, {60, 60, 10, 10});
    second.color = Qt::green;
    (void)root.takeDamage();
    QCOMPARE(first.paintCount, 1);
    QCOMPARE(second.paintCount, 1);
    QCOMPARE(chrome.pixelColor(65, 65), QColor(Qt::green));

    first.color = Qt::blue;
    first.update(first.geometry());
    (void)root.takeDamage();
    QCOMPARE(first.paintCount, 2);
    QCOMPARE(chrome.pixelColor(15, 15), QColor(Qt::blue));
    (void)root.takeDamage();
    QCOMPARE(first.paintCount, 2);
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
    view.name = u"Copy"_s;
    QCOMPARE(accessible->text(QAccessible::Name), u"Copy"_s);
    QCOMPARE(view.accessible(), accessible);
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
