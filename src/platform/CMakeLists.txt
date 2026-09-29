# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Backend interfaces and the capability vocabulary. No code yet: the module declares its place in the graph.
ariadshot_add_module(NAME platform KIND INTERFACE ALLOWED AriadShot::core Qt6::Gui)
target_link_libraries(ariadshot_platform INTERFACE AriadShot::core Qt6::Gui)
