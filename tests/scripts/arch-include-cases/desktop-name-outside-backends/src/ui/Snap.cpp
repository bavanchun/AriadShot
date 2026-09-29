// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QByteArray>
#include <QtGlobal>

bool isHyprland() { return qgetenv("XDG_CURRENT_DESKTOP") == "Hyprland"; }
