# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Models, algorithms and stores: QtCore only.
ariadshot_add_module(NAME core KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_core PRIVATE BuildInfo.h BuildInfo.cpp Expected.h)
target_link_libraries(ariadshot_core PUBLIC Qt6::Core)
target_include_directories(ariadshot_core SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/tl-expected")
ariadshot_add_version_header(ariadshot_core)
