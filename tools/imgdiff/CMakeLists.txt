# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

add_executable(ariadshot-imgdiff main.cpp)
target_link_libraries(ariadshot-imgdiff PRIVATE Qt6::Gui AriadShot::options ariadshot_test_support)
