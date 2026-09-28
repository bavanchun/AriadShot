// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the expected Quit title follows macshot/AppDelegate.swift:914@b4d4f3a (see PROVENANCE.md).

#include "app/TrayController.h"

#include <QApplication>
#include <QMenu>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using ariadshot::app::TrayController;

// Runs offscreen: whether the icon appears on a real tray is checked by hand on the desktop.
class TrayControllerTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void menuHoldsOnlyQuit();
    void quitActionEndsTheEventLoop();

  private:
    static void expectNoTrayWarning();
};

void TrayControllerTest::expectNoTrayWarning() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"No system tray is available"_s));
    }
}

void TrayControllerTest::menuHoldsOnlyQuit() {
    expectNoTrayWarning();
    const TrayController tray;

    const QList<QAction*> actions = tray.menu()->actions();
    QCOMPARE(actions.size(), 1);
    // MacShot's "Quit macshot", with AriadShot's name.
    QCOMPARE(actions.first()->text(), u"Quit AriadShot"_s);
}

void TrayControllerTest::quitActionEndsTheEventLoop() {
    expectNoTrayWarning();
    const TrayController tray;
    QAction* quit = tray.menu()->actions().first();

    QTimer watchdog;
    watchdog.setSingleShot(true);
    connect(&watchdog, &QTimer::timeout, this, [] { QCoreApplication::exit(1); });
    watchdog.start(5000);
    QTimer::singleShot(0, quit, &QAction::trigger);

    QCOMPARE(QApplication::exec(), 0);
}

QTEST_MAIN(TrayControllerTest)
#include "tst_TrayController.moc"
