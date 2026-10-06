# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Backend and host interfaces and the capability vocabulary: interfaces only, no platform headers.
ariadshot_add_module(NAME platform KIND STATIC ALLOWED AriadShot::core Qt6::Gui)
target_sources(ariadshot_platform PRIVATE
    OutputId.h
    SurfaceContent.h
    SurfaceHost.h
    Surfaces.cpp)
target_link_libraries(ariadshot_platform PUBLIC AriadShot::core Qt6::Gui)
