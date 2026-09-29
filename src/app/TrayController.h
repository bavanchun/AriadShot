// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QSystemTrayIcon>

#include <memory>

class QMenu;

namespace ariadshot::app {

// The tray (status) icon and its context menu. The menu holds the items that exist in this build; today that is
// Quit. When the desktop offers no system tray the icon is not shown and the daemon keeps running, as MacShot keeps
// running with its status item hidden (macshot/AppDelegate.swift:796@b4d4f3a). Thread: GUI.
class TrayController : public QObject {
    Q_OBJECT

  public:
    explicit TrayController(QObject* parent = nullptr);
    ~TrayController() override;

    [[nodiscard]] QMenu* menu() const;
    [[nodiscard]] bool isIconVisible() const;

  private:
    // Declared before the icon so that the icon, which refers to the menu, is destroyed first.
    std::unique_ptr<QMenu> m_menu;
    QSystemTrayIcon m_icon;
};

} // namespace ariadshot::app
