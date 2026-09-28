# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# X11 capture, input and window discovery. No code yet; the xcb libraries join with the first X11 code.
ariadshot_add_module(NAME backends/x11 KIND INTERFACE ALLOWED AriadShot::platform)
target_link_libraries(ariadshot_backends_x11 INTERFACE AriadShot::platform)
