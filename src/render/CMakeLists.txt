# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Canonical images and renderers: QtGui, never QtWidgets.
ariadshot_add_module(NAME render KIND STATIC ALLOWED AriadShot::core Qt6::Gui)
target_sources(ariadshot_render PRIVATE
    CanonicalImage.h
    CanonicalImage.cpp
    text/FontSet.h
    text/FontSet.cpp
    pixel/PixelSpan.h
    pixel/PixelSpan.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(ariadshot_render PROPERTIES AUTORCC ON)
    target_sources(ariadshot_render PRIVATE resources/fonts/fonts.qrc)
endif()
target_link_libraries(ariadshot_render PUBLIC AriadShot::core Qt6::Gui)
