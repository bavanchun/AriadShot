// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "app/SignalBridge.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>

#include <csignal>

using namespace Qt::StringLiterals;
using ariadshot::app::SignalBridge;

class SignalBridgeTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void signalEndsTheEventLoopCleanly_data();
    void signalEndsTheEventLoopCleanly();
    void restoresThePreviousHandlers();
    void secondBridgeIsNotInstalled();

  private:
    int runUntilQuit(int signalNumber);
};

int SignalBridgeTest::runUntilQuit(int signalNumber) {
    QTimer watchdog;
    watchdog.setSingleShot(true);
    connect(&watchdog, &QTimer::timeout, this, [] { QCoreApplication::exit(1); });
    watchdog.start(5000);
    QTimer::singleShot(0, this, [signalNumber] { std::raise(signalNumber); });
    return QCoreApplication::exec();
}

void SignalBridgeTest::signalEndsTheEventLoopCleanly_data() {
    QTest::addColumn<int>("signalNumber");
    QTest::newRow("SIGTERM") << SIGTERM;
    QTest::newRow("SIGINT") << SIGINT;
}

void SignalBridgeTest::signalEndsTheEventLoopCleanly() {
    QFETCH(int, signalNumber);
    const SignalBridge bridge;
    QVERIFY(bridge.isInstalled());
    QCOMPARE(runUntilQuit(signalNumber), 0);
}

void SignalBridgeTest::restoresThePreviousHandlers() {
    struct sigaction before{};
    QCOMPARE(::sigaction(SIGTERM, nullptr, &before), 0);
    {
        const SignalBridge bridge;
        struct sigaction during{};
        QCOMPARE(::sigaction(SIGTERM, nullptr, &during), 0);
        QVERIFY(during.sa_handler != before.sa_handler);
    }
    struct sigaction after{};
    QCOMPARE(::sigaction(SIGTERM, nullptr, &after), 0);
    QVERIFY(after.sa_handler == before.sa_handler);
}

void SignalBridgeTest::secondBridgeIsNotInstalled() {
    const SignalBridge first;
    QVERIFY(first.isInstalled());
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"already installed"_s));
        const SignalBridge second;
        QVERIFY(!second.isInstalled());
    }
    // Destroying the second instance leaves the first one working.
    QCOMPARE(runUntilQuit(SIGTERM), 0);
}

QTEST_GUILESS_MAIN(SignalBridgeTest)
#include "tst_SignalBridge.moc"
