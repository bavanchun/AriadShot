# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Host-agnostic view objects, chrome and the canvas text control; QtGui only, never QtWidgets.
ariadshot_add_module(NAME ui KIND STATIC ALLOWED AriadShot::core AriadShot::render AriadShot::platform Qt6::Gui)
target_sources(ariadshot_ui PRIVATE
    ViewObject.h
    ViewObject.cpp
    ViewRoot.h
    ViewRoot.cpp)
target_link_libraries(ariadshot_ui PUBLIC AriadShot::core AriadShot::render AriadShot::platform Qt6::Gui)
