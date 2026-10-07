// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <backends/wayland/OutputRegistry.h>

using namespace Qt::StringLiterals;
using namespace ariadshot::backends::wayland;

class OutputMatchingTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void testExactMatch();
    void testUnmatchedWaylandOutput();
    void testUnmatchedScreenName();
    void testEmptyInputs();
    void testLogicalGeometryCalculation();
    void testMatchScreensWithUnmatchedWarning();
    void testRegistrySignals();
};

// MacShot enumerates all available displays for capture in macshot/Capture/ScreenCaptureManager.swift:401-403@b4d4f3a
// ("let screens = NSScreen.screens") and labels screens by name in
// macshot/UI/Windows/SettingsWindowController.swift:2225-2231@b4d4f3a ("screen.localizedName").
// In AriadShot on Wayland, outputs and seats are bound separately on each connection and matched by wl_output.name
// (v4+) to Qt's QScreen::name() (docs/spec/04-capture-and-overlay.md §3 rule 3).
void OutputMatchingTest::testExactMatch() {
    WaylandOutputInfo out1;
    out1.id = 1;
    out1.name = u"HEADLESS-1"_s;
    out1.currentModeSize = QSize(1920, 1080);
    out1.scale = 1;
    out1.transform = 0; // WL_OUTPUT_TRANSFORM_NORMAL
    out1.done = true;

    WaylandOutputInfo out2;
    out2.id = 2;
    out2.name = u"HEADLESS-2"_s;
    out2.currentModeSize = QSize(1920, 1080);
    out2.scale = 1;
    out2.transform = 3; // WL_OUTPUT_TRANSFORM_270
    out2.done = true;

    const QList<WaylandOutputInfo> outputs = {out1, out2};
    const QStringList screenNames = {u"HEADLESS-1"_s, u"HEADLESS-2"_s};

    const OutputMatchResult result = OutputRegistry::matchOutputs(outputs, screenNames);

    QCOMPARE(result.matched.size(), 2);
    QCOMPARE(result.unmatchedOutputs.size(), 0);
    QCOMPARE(result.unmatchedScreenNames.size(), 0);

    QCOMPARE(result.matched[0].name, u"HEADLESS-1"_s);
    QCOMPARE(result.matched[0].transform, 0);
    QCOMPARE(result.matched[1].name, u"HEADLESS-2"_s);
    QCOMPARE(result.matched[1].transform, 3);
}

// docs/spec/04-capture-and-overlay.md §3 rule 3: "An output that cannot be matched is not captured, and the failure
// is logged at warning level."
void OutputMatchingTest::testUnmatchedWaylandOutput() {
    WaylandOutputInfo out1;
    out1.id = 1;
    out1.name = u"HEADLESS-1"_s;

    WaylandOutputInfo outOrphan;
    outOrphan.id = 99;
    outOrphan.name = u"DP-1"_s;

    const QList<WaylandOutputInfo> outputs = {out1, outOrphan};
    const QStringList screenNames = {u"HEADLESS-1"_s};

    const OutputMatchResult result = OutputRegistry::matchOutputs(outputs, screenNames);

    QCOMPARE(result.matched.size(), 1);
    QCOMPARE(result.matched[0].name, u"HEADLESS-1"_s);

    QCOMPARE(result.unmatchedOutputs.size(), 1);
    QCOMPARE(result.unmatchedOutputs[0].name, u"DP-1"_s);
    QCOMPARE(result.unmatchedScreenNames.size(), 0);
}

void OutputMatchingTest::testUnmatchedScreenName() {
    WaylandOutputInfo out1;
    out1.id = 1;
    out1.name = u"HEADLESS-1"_s;

    const QList<WaylandOutputInfo> outputs = {out1};
    const QStringList screenNames = {u"HEADLESS-1"_s, u"HDMI-A-1"_s};

    const OutputMatchResult result = OutputRegistry::matchOutputs(outputs, screenNames);

    QCOMPARE(result.matched.size(), 1);
    QCOMPARE(result.unmatchedOutputs.size(), 0);

    QCOMPARE(result.unmatchedScreenNames.size(), 1);
    QCOMPARE(result.unmatchedScreenNames[0], u"HDMI-A-1"_s);
}

// MacShot guards against empty screen lists in macshot/Services/ScreenFallback.swift:8-14@b4d4f3a
// ("NSScreen.screens can be empty — while every display is asleep").
void OutputMatchingTest::testEmptyInputs() {
    const OutputMatchResult emptyBoth = OutputRegistry::matchOutputs({}, {});
    QCOMPARE(emptyBoth.matched.size(), 0);
    QCOMPARE(emptyBoth.unmatchedOutputs.size(), 0);
    QCOMPARE(emptyBoth.unmatchedScreenNames.size(), 0);

    WaylandOutputInfo out1;
    out1.id = 1;
    out1.name = u"HEADLESS-1"_s;

    const OutputMatchResult emptyScreens = OutputRegistry::matchOutputs({out1}, {});
    QCOMPARE(emptyScreens.matched.size(), 0);
    QCOMPARE(emptyScreens.unmatchedOutputs.size(), 1);
    QCOMPARE(emptyScreens.unmatchedScreenNames.size(), 0);

    const OutputMatchResult emptyOutputs = OutputRegistry::matchOutputs({}, {u"HEADLESS-1"_s});
    QCOMPARE(emptyOutputs.matched.size(), 0);
    QCOMPARE(emptyOutputs.unmatchedOutputs.size(), 0);
    QCOMPARE(emptyOutputs.unmatchedScreenNames.size(), 1);
}

void OutputMatchingTest::testLogicalGeometryCalculation() {
    // 1. Standard 1x unrotated output
    WaylandOutputInfo standard;
    standard.geometryPosition = QPoint(0, 0);
    standard.currentModeSize = QSize(1920, 1080);
    standard.scale = 1;
    standard.transform = 0; // WL_OUTPUT_TRANSFORM_NORMAL

    QCOMPARE(standard.logicalSize(), QSize(1920, 1080));
    QCOMPARE(standard.logicalGeometry(), QRect(0, 0, 1920, 1080));

    // 2. High-DPI 2x unrotated output positioned next to standard
    WaylandOutputInfo hidpi;
    hidpi.geometryPosition = QPoint(1920, 0);
    hidpi.currentModeSize = QSize(3840, 2160);
    hidpi.scale = 2;
    hidpi.transform = 0;

    QCOMPARE(hidpi.logicalSize(), QSize(1920, 1080));
    QCOMPARE(hidpi.logicalGeometry(), QRect(1920, 0, 1920, 1080));

    // 3. Rotated 90 degrees output: buffer mode is 1920x1080, logical size swaps to 1080x1920
    WaylandOutputInfo rotated90;
    rotated90.geometryPosition = QPoint(3840, 0);
    rotated90.currentModeSize = QSize(1920, 1080);
    rotated90.scale = 1;
    rotated90.transform = 1; // WL_OUTPUT_TRANSFORM_90

    QCOMPARE(rotated90.logicalSize(), QSize(1080, 1920));
    QCOMPARE(rotated90.logicalGeometry(), QRect(3840, 0, 1080, 1920));

    // 4. Rotated 270 degrees output (like HEADLESS-2 in headless Sway harness)
    WaylandOutputInfo rotated270;
    rotated270.geometryPosition = QPoint(0, 1080);
    rotated270.currentModeSize = QSize(1920, 1080);
    rotated270.scale = 1;
    rotated270.transform = 3; // WL_OUTPUT_TRANSFORM_270

    QCOMPARE(rotated270.logicalSize(), QSize(1080, 1920));
    QCOMPARE(rotated270.logicalGeometry(), QRect(0, 1080, 1080, 1920));
}

void OutputMatchingTest::testMatchScreensWithUnmatchedWarning() {
    OutputRegistry registry;

    WaylandOutputInfo out1;
    out1.id = 1;
    out1.name = u"HEADLESS-1"_s;
    out1.currentModeSize = QSize(1280, 720);
    out1.done = true;

    WaylandOutputInfo outOrphan;
    outOrphan.id = 2;
    outOrphan.name = u"DP-1"_s;
    outOrphan.currentModeSize = QSize(1920, 1080);
    outOrphan.done = true;

    registry.handleOutputDone(out1.id, out1);
    registry.handleOutputDone(outOrphan.id, outOrphan);

    QTest::ignoreMessage(QtWarningMsg, "Wayland output DP-1 cannot be matched to any Qt screen; capture will skip it");

    const OutputMatchResult result = registry.matchScreens({u"HEADLESS-1"_s});
    QCOMPARE(result.matched.size(), 1);
    QCOMPARE(result.matched[0].name, u"HEADLESS-1"_s);
    QCOMPARE(result.unmatchedOutputs.size(), 1);
    QCOMPARE(result.unmatchedOutputs[0].name, u"DP-1"_s);
    QCOMPARE(result.unmatchedScreenNames.size(), 0);
}

void OutputMatchingTest::testRegistrySignals() {
    OutputRegistry registry;
    QSignalSpy spyAdded(&registry, &OutputRegistry::outputAdded);
    QSignalSpy spyChanged(&registry, &OutputRegistry::outputChanged);
    QSignalSpy spyRemoved(&registry, &OutputRegistry::outputRemoved);

    WaylandOutputInfo out;
    out.id = 42;
    out.name = u"HDMI-A-1"_s;
    out.currentModeSize = QSize(1920, 1080);
    out.scale = 1;
    out.done = true;

    // First arrival emits outputAdded
    registry.handleOutputDone(out.id, out);
    QCOMPARE(spyAdded.count(), 1);
    QCOMPARE(spyChanged.count(), 0);
    QCOMPARE(spyRemoved.count(), 0);
    QCOMPARE(registry.outputs().size(), 1);
    QCOMPARE(registry.findByName(u"HDMI-A-1"_s).name, u"HDMI-A-1"_s);

    // Update to existing output emits outputChanged
    out.scale = 2;
    registry.handleOutputDone(out.id, out);
    QCOMPARE(spyAdded.count(), 1);
    QCOMPARE(spyChanged.count(), 1);
    QCOMPARE(spyRemoved.count(), 0);
    QCOMPARE(registry.findByName(u"HDMI-A-1"_s).scale, 2);

    // Removal emits outputRemoved
    registry.removeOutput(out.id);
    QCOMPARE(spyAdded.count(), 1);
    QCOMPARE(spyChanged.count(), 1);
    QCOMPARE(spyRemoved.count(), 1);
    QCOMPARE(registry.outputs().size(), 0);

    const QList<QVariant> removeArgs = spyRemoved.takeFirst();
    QCOMPARE(removeArgs.at(0).toUInt(), 42U);
    QCOMPARE(removeArgs.at(1).toString(), u"HDMI-A-1"_s);
}

QTEST_GUILESS_MAIN(OutputMatchingTest)
#include "tst_OutputMatching.moc"
