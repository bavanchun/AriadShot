# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Qt Widgets windows and dialogs. No code yet.
ariadshot_add_module(NAME windows KIND INTERFACE ALLOWED AriadShot::ui AriadShot::render AriadShot::core Qt6::Widgets)
target_link_libraries(ariadshot_windows INTERFACE AriadShot::ui AriadShot::render AriadShot::core Qt6::Widgets)
