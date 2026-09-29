// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "app/SignalBridge.h"
#include "app/TrayController.h"
#include "core/BuildInfo.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char* argv[]) {
    using ariadshot::core::BuildInfo;

    QCoreApplication::setApplicationName(BuildInfo::applicationName());
    QCoreApplication::setOrganizationDomain(BuildInfo::organizationDomain());
    QCoreApplication::setApplicationVersion(BuildInfo::version());
    QGuiApplication::setDesktopFileName(BuildInfo::applicationId());

    QApplication application(argc, argv);
    // The daemon lives in the tray; closing a window must not end it.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "AriadShot screenshot and screen recording daemon"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(application);

    const ariadshot::app::SignalBridge signalBridge;
    const ariadshot::app::TrayController trayController;

    return QApplication::exec();
}
