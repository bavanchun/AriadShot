// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <backends/wayland/OutputRegistry.h>
#include <backends/wayland/WaylandSession.h>
#include <cstdint>
#include <thread>
#include <wayland-client-protocol.h>

using namespace Qt::StringLiterals;
using namespace ariadshot::backends::wayland;

class WaylandSessionTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void sessionConnectsDispatchesAndShutsDownCleanly();
};

void WaylandSessionTest::sessionConnectsDispatchesAndShutsDownCleanly() {
    const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    if (waylandDisplay.isEmpty()) {
        QFAIL("WAYLAND_DISPLAY is not set or empty; headless Sway fixture is required");
    }

    WaylandSession session;
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());

    QVERIFY(session.connectAndStart());
    QVERIFY(session.isConnected());
    QVERIFY(session.isDispatching());

    // Assert that the session's dispatch thread is distinct from the test's main thread
    const std::thread::id dispatchThreadId = session.dispatchThreadId();
    QVERIFY(dispatchThreadId != std::this_thread::get_id());

    OutputRegistry* registry = session.outputRegistry();
    QVERIFY(registry != nullptr);

    // Wait for the two headless outputs (HEADLESS-1 and HEADLESS-2) configured by run-headless-sway.sh
    QTRY_COMPARE_WITH_TIMEOUT(registry->outputs().size(), 2, 5000);

    const auto headless1 = registry->findByName(u"HEADLESS-1"_s);
    QCOMPARE(headless1.name, u"HEADLESS-1"_s);
    QVERIFY(headless1.done);
    QCOMPARE(headless1.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_NORMAL));

    const auto headless2 = registry->findByName(u"HEADLESS-2"_s);
    QCOMPARE(headless2.name, u"HEADLESS-2"_s);
    QVERIFY(headless2.done);
    // Sway inverts transform angles (invert_rotation_direction()), so "transform 90" produces WL_OUTPUT_TRANSFORM_270
    QCOMPARE(headless2.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_270));

    // Test output matching against known screen names
    const QStringList screenNames = {u"HEADLESS-1"_s, u"HEADLESS-2"_s};
    const OutputMatchResult match = registry->matchScreens(screenNames);
    QCOMPARE(match.matched.size(), 2);
    QCOMPARE(match.unmatchedOutputs.size(), 0);
    QCOMPARE(match.unmatchedScreenNames.size(), 0);

    // Shutdown cleanly
    session.disconnectAndStop();
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());
}

QTEST_GUILESS_MAIN(WaylandSessionTest)
#include "tst_WaylandSession.moc"
