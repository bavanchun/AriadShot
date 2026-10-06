# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

if(ARIADSHOT_MEDIA)
    add_executable(ariadshot-bench-studio-preview
        studio-contract/PreviewHarness.cpp
    )

    set_target_properties(ariadshot-bench-studio-preview PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/studio-contract"
    )

    target_link_libraries(ariadshot-bench-studio-preview PRIVATE
        AriadShot::media
        AriadShot::options
        ariadshot_test_support
        Qt6::Widgets
        Qt6::GuiPrivate
    )
endif()
