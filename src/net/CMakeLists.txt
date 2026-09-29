# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Every HTTP request of the application. No code yet; Qt6::Network and Qt6::NetworkAuth are found here, behind an
# option that defaults to OFF, when the first request lands.
ariadshot_add_module(NAME net KIND INTERFACE ALLOWED AriadShot::core Qt6::Network Qt6::NetworkAuth)
target_link_libraries(ariadshot_net INTERFACE AriadShot::core)
