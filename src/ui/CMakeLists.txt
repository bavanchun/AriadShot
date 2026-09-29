# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Host-agnostic view objects, chrome and the canvas text control; QtGui only, never QtWidgets. No code yet.
ariadshot_add_module(NAME ui KIND INTERFACE ALLOWED AriadShot::core AriadShot::render AriadShot::platform Qt6::Gui)
target_link_libraries(ariadshot_ui INTERFACE AriadShot::core AriadShot::render AriadShot::platform Qt6::Gui)
