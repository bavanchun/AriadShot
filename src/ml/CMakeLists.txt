# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Recognition interfaces and the Linux engines. No code yet; never links Qt6::Network or net.
ariadshot_add_module(NAME ml KIND INTERFACE ALLOWED AriadShot::core Qt6::Gui)
target_link_libraries(ariadshot_ml INTERFACE AriadShot::core Qt6::Gui)
