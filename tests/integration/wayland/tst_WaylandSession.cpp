// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-client-protocol.h>

using namespace Qt::StringLiterals;
using namespace ariadshot::backends::wayland;

namespace {

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
    void sessionHandlesProtocolError();
    void sessionConnectAndStartDoesNotHangOnImmediateClose();
    void sessionReconnectsAfterConnectionLoss();
};

void WaylandSessionTest::sessionConnectsDispatchesAndShutsDownCleanly() {
    const QString waylandDisplay = QString::fromUtf8(qgetenv("WAYLAND_DISPLAY"));
    if (waylandDisplay.isEmpty()) {
        QFAIL("WAYLAND_DISPLAY is not set or empty; headless Sway fixture is required");
    }
    const QString swaySock = QString::fromUtf8(qgetenv("SWAYSOCK"));
    if (swaySock.isEmpty()) {
        QFAIL("SWAYSOCK is not set or empty; headless Sway fixture with published IPC socket is required");
    }

    const QString expectedIpc = waylandDisplay.endsWith(u".sock"_s)
                                    ? (waylandDisplay.left(waylandDisplay.length() - 5) + u"-ipc.sock"_s)
                                    : (waylandDisplay + u"-ipc.sock"_s);
    const QFileInfo swayInfo(swaySock);
    const QFileInfo expectedInfo(expectedIpc);
    if (swayInfo.absoluteFilePath() != expectedInfo.absoluteFilePath()) {
        QFAIL(qPrintable(u"SWAYSOCK '%1' does not match expected harness IPC path '%2'"_s.arg(swaySock, expectedIpc)));
    }
    if (!swayInfo.exists()) {
        QFAIL(qPrintable(u"SWAYSOCK '%1' does not exist"_s.arg(swaySock)));
    }
    const QFileInfo waylandInfo(waylandDisplay);
    if (!waylandInfo.exists()) {
        QFAIL(qPrintable(u"WAYLAND_DISPLAY '%1' does not exist"_s.arg(waylandDisplay)));
    }
    if (swayInfo.canonicalPath() != waylandInfo.canonicalPath()) {
        QFAIL(qPrintable(u"SWAYSOCK target '%1' is not in harness runtime directory '%2'"_s.arg(
            swayInfo.canonicalPath(), waylandInfo.canonicalPath())));
    }

    WaylandSession session;
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());

    std::mutex threadIdsMutex;
    std::vector<std::thread::id> outputAddedThreadIds;
    QObject::connect(
        session.outputRegistry(), &OutputRegistry::outputAdded, &session,
        [&outputAddedThreadIds, &threadIdsMutex](const WaylandOutputInfo&) {
            std::scoped_lock lock(threadIdsMutex);
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

    // Assert that outputAdded was dispatched on the dedicated session dispatch thread (safely synchronized)
    auto threadIdCount = [&]() {
        std::scoped_lock lock(threadIdsMutex);
        return outputAddedThreadIds.size();
    };
    QTRY_COMPARE_WITH_TIMEOUT(threadIdCount(), 2UL, 5000);

    {
        std::scoped_lock lock(threadIdsMutex);
        for (const auto& tid : outputAddedThreadIds) {
            QCOMPARE(tid, dispatchThreadId);
            QVERIFY(tid != std::this_thread::get_id());
        }
    }

    const auto headless1 = registry->findByName(u"HEADLESS-1"_s);
    QCOMPARE(headless1.name, u"HEADLESS-1"_s);
    QVERIFY(headless1.done);
    QCOMPARE(headless1.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_NORMAL));
    QCOMPARE(headless1.scale, 1);
    QCOMPARE(headless1.currentModeSize, QSize(1280, 720));
    QCOMPARE(headless1.bufferDerivedSize(), QSize(1280, 720));

    const auto headless2 = registry->findByName(u"HEADLESS-2"_s);
    QCOMPARE(headless2.name, u"HEADLESS-2"_s);
    QVERIFY(headless2.done);
    // Sway inverts transform angles (invert_rotation_direction()), so "transform 90" produces WL_OUTPUT_TRANSFORM_270
    QCOMPARE(headless2.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_270));
    QCOMPARE(headless2.scale, 1);
    QCOMPARE(headless2.currentModeSize, QSize(1920, 1080));
    // Rotated 270 deg swaps width and height: 1920x1080 mode becomes 1080x1920 buffer-derived size
    QCOMPARE(headless2.bufferDerivedSize(), QSize(1080, 1920));

    // Test output matching against known screen names
    const QStringList screenNames = {u"HEADLESS-1"_s, u"HEADLESS-2"_s};
    const OutputMatchResult match = registry->matchScreens(screenNames);
    QCOMPARE(match.matched.size(), 2);
    QCOMPARE(match.unmatchedOutputs.size(), 0);
    QCOMPARE(match.unmatchedScreenNames.size(), 0);

    // Test hotplug behavior through the harness-started Sway IPC socket
    QSignalSpy spyChanged(registry, &OutputRegistry::outputChanged);
    QSignalSpy spyRemoved(registry, &OutputRegistry::outputRemoved);

    // 1. Dynamic output configuration change: change scale of HEADLESS-1
    QVERIFY(runSwayCommand(swaySock, {u"output"_s, u"HEADLESS-1"_s, u"scale"_s, u"2"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(spyChanged.count() >= 1, 5000);
    QCOMPARE(registry->findByName(u"HEADLESS-1"_s).scale, 2);

    // Restore scale
    QVERIFY(runSwayCommand(swaySock, {u"output"_s, u"HEADLESS-1"_s, u"scale"_s, u"1"_s}));
    QTRY_COMPARE_WITH_TIMEOUT(registry->findByName(u"HEADLESS-1"_s).scale, 1, 5000);

    // 2. Output removal: disable HEADLESS-2
    QVERIFY(runSwayCommand(swaySock, {u"output"_s, u"HEADLESS-2"_s, u"disable"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(spyRemoved.count() >= 1, 5000);
    QCOMPARE(registry->outputs().size(), 1);

    // 3. Re-enable HEADLESS-2
    QVERIFY(runSwayCommand(swaySock, {u"output"_s, u"HEADLESS-2"_s, u"enable"_s}));
    QVERIFY(runSwayCommand(swaySock, {u"output"_s, u"HEADLESS-2"_s, u"transform"_s, u"90"_s}));
    QTRY_COMPARE_WITH_TIMEOUT(registry->outputs().size(), 2, 5000);

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

void WaylandSessionTest::sessionHandlesProtocolError() {
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QFAIL("Temporary directory is invalid");
    }
    const QString sockPath = tempDir.filePath(u"proto-err.sock"_s);
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

    // Write one wl_display.error event (object 1, opcode 0)
    struct WireErrorEvent {
        uint32_t senderId = 1;
        uint32_t sizeAndOpcode = (28U << 16U) | 0U;
        uint32_t objectId = 1;
        uint32_t code = 0;
        uint32_t strLen = 6;
        char str[8] = {'e', 'r', 'r', 'o', 'r', '\0', '\0', '\0'};
    } errorEvent;

    const ssize_t written = write(clientFd, &errorEvent, sizeof(errorEvent));
    QCOMPARE(written, static_cast<ssize_t>(sizeof(errorEvent)));

    close(clientFd);
    close(serverFd);

    // The dispatch thread should detect protocol error, exit loop, clear isConnected, and emit connectionLost
    QTRY_COMPARE_WITH_TIMEOUT(lostSpy.count(), 1, 5000);
    QVERIFY(!session.isConnected());
    QVERIFY(!session.isDispatching());

    session.disconnectAndStop();
    QVERIFY(!session.isConnected());
}

void WaylandSessionTest::sessionConnectAndStartDoesNotHangOnImmediateClose() {
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QFAIL("Temporary directory is invalid");
    }
    const QString sockPath = tempDir.filePath(u"hang-check.sock"_s);
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

    // Accept and immediately close on server side in background
    std::thread serverThread([serverFd]() {
        const int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd >= 0) {
            close(clientFd);
        }
        close(serverFd);
    });

    WaylandSession session(sockPath);
    // connectAndStart must return without hanging even if the thread exits immediately on EOF
    const bool started = session.connectAndStart();
    serverThread.join();

    if (started) {
        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(), 5000);
        QVERIFY(!session.isDispatching());
        session.disconnectAndStop();
    }
    QVERIFY(!session.isConnected());
}

void WaylandSessionTest::sessionReconnectsAfterConnectionLoss() {
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QFAIL("Temporary directory is invalid");
    }

    auto createServer = [](const QString& path) -> int {
        const QByteArray bytes = path.toUtf8();
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, bytes.constData(), sizeof(addr.sun_path) - 1);
        if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 || listen(fd, 1) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    };

    const QString sockPath1 = tempDir.filePath(u"dummy1.sock"_s);
    const QString sockPath2 = tempDir.filePath(u"dummy2.sock"_s);

    int srv1 = createServer(sockPath1);
    if (srv1 < 0) {
        QFAIL("Failed to create server 1");
    }

    WaylandSession session(sockPath1);
    QSignalSpy lostSpy(&session, &WaylandSession::connectionLost);
    QSignalSpy disconnectedSpy(&session, &WaylandSession::disconnected);

    QVERIFY(session.connectAndStart());
    QVERIFY(session.isConnected());

    int client1 = accept(srv1, nullptr, nullptr);
    if (client1 < 0) {
        close(srv1);
        QFAIL("Failed to accept client 1");
    }

    // Drop connection 1
    close(client1);
    close(srv1);

    // Verify connectionLost arrives, and disconnected was NOT emitted
    QTRY_COMPARE_WITH_TIMEOUT(lostSpy.count(), 1, 5000);
    QCOMPARE(disconnectedSpy.count(), 0);
    QVERIFY(!session.isConnected());

    // Start server 2
    int srv2 = createServer(sockPath2);
    if (srv2 < 0) {
        QFAIL("Failed to create server 2");
    }

    // Reconnect to server 2 using connectAndStart without calling disconnectAndStop first
    session.setDisplayName(sockPath2);
    QVERIFY(session.connectAndStart());
    QVERIFY(session.isConnected());

    int client2 = accept(srv2, nullptr, nullptr);
    if (client2 < 0) {
        close(srv2);
        QFAIL("Failed to accept client 2");
    }

    // Clean shutdown
    session.disconnectAndStop();
    QCOMPARE(disconnectedSpy.count(), 1);
    QVERIFY(!session.isConnected());

    close(client2);
    close(srv2);
}

QTEST_GUILESS_MAIN(WaylandSessionTest)
#include "tst_WaylandSession.moc"
