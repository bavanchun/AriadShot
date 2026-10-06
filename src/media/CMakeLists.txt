# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Recorder and studio.
if(APPLE)
    set(ARIADSHOT_MEDIA_DEFAULT OFF)
else()
    set(ARIADSHOT_MEDIA_DEFAULT ON)
endif()
option(ARIADSHOT_MEDIA "Enable media recorder and studio compositor" ${ARIADSHOT_MEDIA_DEFAULT})

if(NOT ARIADSHOT_MEDIA)
    ariadshot_add_module(NAME media KIND INTERFACE ALLOWED AriadShot::core AriadShot::render Qt6::GuiPrivate)
    target_link_libraries(ariadshot_media INTERFACE AriadShot::core AriadShot::render)
    return()
endif()

# When ARIADSHOT_MEDIA is ON, every package it needs must be found.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
find_package(Qt6 REQUIRED COMPONENTS ShaderTools)

# Cache variable to allow forcing Vulkan support off for testing builds without Vulkan.
option(ARIADSHOT_ENABLE_VULKAN "Enable Vulkan support when available in Qt and Vulkan headers are found" ON)

# Determine whether Qt's Vulkan support is actually usable in this build.
# Qt's qvulkaninstance.h and QRhi Vulkan backend require Vulkan headers (WrapVulkanHeaders / Vulkan::Headers)
# to be present at application build time. If missing (e.g. linux-qt-floor CI), Vulkan cannot be used.
set(ARIADSHOT_HAVE_VULKAN FALSE CACHE INTERNAL "Whether Qt Vulkan support is usable in this build")
if(ARIADSHOT_MEDIA AND ARIADSHOT_ENABLE_VULKAN)
    if((TARGET WrapVulkanHeaders::WrapVulkanHeaders OR TARGET Vulkan::Headers OR WrapVulkanHeaders_FOUND)
       AND (NOT DEFINED QT_FEATURE_vulkan OR QT_FEATURE_vulkan))
        set(ARIADSHOT_HAVE_VULKAN TRUE CACHE INTERNAL "Whether Qt Vulkan support is usable in this build")
    endif()
endif()

ariadshot_add_module(NAME media KIND STATIC ALLOWED AriadShot::core AriadShot::render Qt6::GuiPrivate)

if(ARIADSHOT_HAVE_VULKAN)
    target_compile_definitions(ariadshot_media PUBLIC ARIADSHOT_HAVE_VULKAN)
endif()

target_sources(ariadshot_media PRIVATE
    studio/FrameScene.h
    studio/FrameScene.cpp
    studio/StudioCompositor.h
    studio/StudioCompositor.cpp
)

target_link_libraries(ariadshot_media PUBLIC
    AriadShot::core
    AriadShot::render
    Qt6::GuiPrivate
)

qt_add_shaders(ariadshot_media "studio_shaders"
    PREFIX "/media/studio/shaders"
    BASE "studio/shaders"
    FILES
        studio/shaders/fullscreen.vert
        studio/shaders/source_linear.frag
        studio/shaders/overlay_blend.frag
        studio/shaders/linear_to_srgb.frag
)
