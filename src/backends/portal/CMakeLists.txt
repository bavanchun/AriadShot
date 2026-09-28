# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# XDG desktop portals and desktop D-Bus services. No code yet; Qt6::DBus is found here with the first portal client.
ariadshot_add_module(NAME backends/portal KIND INTERFACE ALLOWED AriadShot::platform Qt6::DBus)
target_link_libraries(ariadshot_backends_portal INTERFACE AriadShot::platform)
