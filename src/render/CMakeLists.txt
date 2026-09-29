# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Canonical images and renderers: QtGui, never QtWidgets.
ariadshot_add_module(NAME render KIND STATIC ALLOWED AriadShot::core Qt6::Gui)
set_target_properties(ariadshot_render PROPERTIES AUTORCC ON)
target_sources(ariadshot_render PRIVATE
    CanonicalImage.h
    CanonicalImage.cpp
    text/FontSet.h
    text/FontSet.cpp
    resources/fonts/fonts.qrc)
target_link_libraries(ariadshot_render PUBLIC AriadShot::core Qt6::Gui)
