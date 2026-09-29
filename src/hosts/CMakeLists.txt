# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Surface hosts for overlay-class surfaces. No code yet; further dependencies follow the overlay host decision.
ariadshot_add_module(NAME hosts KIND INTERFACE ALLOWED AriadShot::ui AriadShot::platform Qt6::Widgets)
target_link_libraries(ariadshot_hosts INTERFACE AriadShot::ui AriadShot::platform Qt6::Widgets)
