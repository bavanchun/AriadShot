// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-FileCopyrightText: sw33tLie and MacShot contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Provenance: the Quit item title follows macshot/AppDelegate.swift:914@b4d4f3a, with AriadShot's name in place of
// MacShot's (see PROVENANCE.md).

#include "app/TrayController.h"

#include "app/Logging.h"
#include "core/BuildInfo.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyle>

namespace ariadshot::app {

TrayController::TrayController(QObject* parent) : QObject(parent), m_menu(std::make_unique<QMenu>()) {
    // MacShot's status menu ends with "Quit macshot".
    QAction* quit = m_menu->addAction(tr("Quit AriadShot", "tray menu item that exits the application"));
    connect(quit, &QAction::triggered, this, [] { QCoreApplication::quit(); });

    const QIcon fallback = QApplication::style()->standardIcon(QStyle::SP_DesktopIcon);
    m_icon.setIcon(QIcon::fromTheme(QStringLiteral("applets-screenshooter"), fallback));
    m_icon.setToolTip(core::BuildInfo::applicationName());
    m_icon.setContextMenu(m_menu.get());

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_icon.show();
    } else {
        qCWarning(lcApp) << "No system tray is available; AriadShot keeps running without a tray icon";
    }
}

TrayController::~TrayController() = default;

QMenu* TrayController::menu() const { return m_menu.get(); }

bool TrayController::isIconVisible() const { return m_icon.isVisible(); }

} // namespace ariadshot::app
