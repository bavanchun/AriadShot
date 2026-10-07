// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <backends/wayland/OutputRegistry.h>
#include <backends/wayland/WaylandSession.h>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-client-protocol.h>

using namespace Qt::StringLiterals;
using namespace ariadshot::backends::wayland;

namespace {

QString findSwayIpcSocket() {
    const QByteArray envSock = qgetenv("SWAYSOCK");
    if (!envSock.isEmpty()) {
        return QString::fromUtf8(envSock);
    }

    // Look for sway-state files recorded by run-headless-sway.sh
    const QList<QDir> candidateDirs = {
        QDir(QDir::current().filePath(u".agent/processes"_s)),
        QDir(u"../../.agent/processes"_s),
        QDir(u"../../../.agent/processes"_s),
    };

    for (const auto& procDir : candidateDirs) {
        if (!procDir.exists()) {
            continue;
        }
        const QStringList stateFiles = procDir.entryList({u"*.sway-state"_s}, QDir::Files);
        for (const auto& sf : stateFiles) {
            QFile file(procDir.filePath(sf));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!file.atEnd()) {
                    const QString line = QString::fromUtf8(file.readLine()).trimmed();
                    if (line.startsWith(u"runtime_dir="_s)) {
                        const QString runtimeDir = line.sliced(12);
                        const QDir rDir(runtimeDir);
                        const QStringList sockets = rDir.entryList({u"sway-ipc.*.sock"_s}, QDir::System);
                        if (!sockets.isEmpty()) {
                            return rDir.filePath(sockets.first());
                        }
                    }
                }
            }
        }
    }
    return {};
}

bool runSwayCommand(const QString& swayIpcSock, const QStringList& args) {
    if (swayIpcSock.isEmpty()) {
        return false;
    }
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"SWAYSOCK"_s, swayIpcSock);
    proc.setProcessEnvironment(env);
    proc.start(u"swaymsg"_s, args);
    return proc.waitForFinished(5000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

} // namespace

class WaylandSessionTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void sessionConnectsDispatchesAndShutsDownCleanly();
    void sessionConnectsWithExplicitDisplayName();
    void sessionHandlesConnectionLoss();
};

void WaylandSessionTest::sessionConnectsDispatchesAndShutsDownCleanly() {
    const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    if (waylandDisplay.isEmpty()) {
        QFAIL("WAYLAND_DISPLAY is not set or empty; headless Sway fixture is required");
    }

    WaylandSession session;
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());

    std::vector<std::thread::id> outputAddedThreadIds;
    QObject::connect(
        session.outputRegistry(), &OutputRegistry::outputAdded, &session,
        [&outputAddedThreadIds](const WaylandOutputInfo&) {
            outputAddedThreadIds.push_back(std::this_thread::get_id());
        },
        Qt::DirectConnection);

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

    // Assert that outputAdded was dispatched on the dedicated session dispatch thread
    QCOMPARE(outputAddedThreadIds.size(), 2UL);
    for (const auto& tid : outputAddedThreadIds) {
        QCOMPARE(tid, dispatchThreadId);
        QVERIFY(tid != std::this_thread::get_id());
    }

    const auto headless1 = registry->findByName(u"HEADLESS-1"_s);
    QCOMPARE(headless1.name, u"HEADLESS-1"_s);
    QVERIFY(headless1.done);
    QCOMPARE(headless1.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_NORMAL));
    QCOMPARE(headless1.scale, 1);
    QCOMPARE(headless1.currentModeSize, QSize(1280, 720));
    QCOMPARE(headless1.geometryPosition, QPoint(0, 0));
    QCOMPARE(headless1.logicalSize(), QSize(1280, 720));
    QCOMPARE(headless1.logicalGeometry(), QRect(0, 0, 1280, 720));

    const auto headless2 = registry->findByName(u"HEADLESS-2"_s);
    QCOMPARE(headless2.name, u"HEADLESS-2"_s);
    QVERIFY(headless2.done);
    // Sway inverts transform angles (invert_rotation_direction()), so "transform 90" produces WL_OUTPUT_TRANSFORM_270
    QCOMPARE(headless2.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_270));
    QCOMPARE(headless2.scale, 1);
    QCOMPARE(headless2.currentModeSize, QSize(1920, 1080));
    QCOMPARE(headless2.geometryPosition, QPoint(0, 0));
    // Rotated 270 deg swaps width and height: 1920x1080 mode becomes 1080x1920 logical size
    QCOMPARE(headless2.logicalSize(), QSize(1080, 1920));
    QCOMPARE(headless2.logicalGeometry(), QRect(0, 0, 1080, 1920));

    // Test output matching against known screen names
    const QStringList screenNames = {u"HEADLESS-1"_s, u"HEADLESS-2"_s};
    const OutputMatchResult match = registry->matchScreens(screenNames);
    QCOMPARE(match.matched.size(), 2);
    QCOMPARE(match.unmatchedOutputs.size(), 0);
    QCOMPARE(match.unmatchedScreenNames.size(), 0);

    // Test hotplug behavior if Sway IPC is available
    const QString swayIpc = findSwayIpcSocket();
    if (!swayIpc.isEmpty()) {
        QSignalSpy spyChanged(registry, &OutputRegistry::outputChanged);
        QSignalSpy spyRemoved(registry, &OutputRegistry::outputRemoved);

        // 1. Dynamic output configuration change: change scale of HEADLESS-1
        QVERIFY(runSwayCommand(swayIpc, {u"output"_s, u"HEADLESS-1"_s, u"scale"_s, u"2"_s}));
        QTRY_VERIFY_WITH_TIMEOUT(spyChanged.count() >= 1, 5000);
        QCOMPARE(registry->findByName(u"HEADLESS-1"_s).scale, 2);

        // Restore scale
        QVERIFY(runSwayCommand(swayIpc, {u"output"_s, u"HEADLESS-1"_s, u"scale"_s, u"1"_s}));
        QTRY_COMPARE_WITH_TIMEOUT(registry->findByName(u"HEADLESS-1"_s).scale, 1, 5000);

        // 2. Output removal: disable HEADLESS-2
        QVERIFY(runSwayCommand(swayIpc, {u"output"_s, u"HEADLESS-2"_s, u"disable"_s}));
        QTRY_VERIFY_WITH_TIMEOUT(spyRemoved.count() >= 1, 5000);
        QCOMPARE(registry->outputs().size(), 1);

        // 3. Re-enable HEADLESS-2
        QVERIFY(runSwayCommand(swayIpc, {u"output"_s, u"HEADLESS-2"_s, u"enable"_s}));
        QVERIFY(runSwayCommand(swayIpc, {u"output"_s, u"HEADLESS-2"_s, u"transform"_s, u"90"_s}));
        QTRY_COMPARE_WITH_TIMEOUT(registry->outputs().size(), 2, 5000);
    }

    // Shutdown cleanly
    session.disconnectAndStop();
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());
}

void WaylandSessionTest::sessionConnectsWithExplicitDisplayName() {
    const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    if (waylandDisplay.isEmpty()) {
        QFAIL("WAYLAND_DISPLAY is not set or empty; headless Sway fixture is required");
    }

    // Pass socket name explicitly to verify display-name string lifetime
    WaylandSession session(QString::fromUtf8(waylandDisplay));
    QVERIFY(session.connectAndStart());
    QVERIFY(session.isConnected());
    QVERIFY(session.isDispatching());

    QTRY_COMPARE_WITH_TIMEOUT(session.outputRegistry()->outputs().size(), 2, 5000);

    session.disconnectAndStop();
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());
}

void WaylandSessionTest::sessionHandlesConnectionLoss() {
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QFAIL("Temporary directory is invalid");
    }
    const QString sockPath = tempDir.filePath(u"dummy-wayland.sock"_s);
    const QByteArray sockBytes = sockPath.toUtf8();

    const int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        QFAIL("Failed to create socket");
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockBytes.constData(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(serverFd);
        QFAIL("Failed to bind socket");
    }
    if (listen(serverFd, 1) != 0) {
        close(serverFd);
        QFAIL("Failed to listen on socket");
    }

    WaylandSession session(sockPath);
    QSignalSpy lostSpy(&session, &WaylandSession::connectionLost);

    QVERIFY(session.connectAndStart());
    QVERIFY(session.isConnected());

    const int clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0) {
        close(serverFd);
        QFAIL("Failed to accept client connection");
    }

    // Simulate compositor termination / connection drop by closing server side
    close(clientFd);
    close(serverFd);

    // The dispatch thread should detect EOF, exit the loop, clear isConnected, and emit connectionLost
    QTRY_COMPARE_WITH_TIMEOUT(lostSpy.count(), 1, 5000);
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());

    session.disconnectAndStop();
    QVERIFY(!session.isConnected());
}

QTEST_GUILESS_MAIN(WaylandSessionTest)
#include "tst_WaylandSession.moc"
