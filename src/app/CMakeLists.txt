# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# The composition root as a library, so it is testable without the executable, and the thin daemon executable.
ariadshot_add_module(NAME app KIND STATIC ALLOWED AriadShot::core AriadShot::render Qt6::Widgets)
target_sources(ariadshot_app PRIVATE
    Logging.h Logging.cpp
    SignalBridge.h SignalBridge.cpp
    TrayController.h TrayController.cpp)
target_link_libraries(ariadshot_app PUBLIC AriadShot::core AriadShot::render Qt6::Widgets)

qt_add_executable(ariadshot-daemon main.cpp)
target_link_libraries(ariadshot-daemon PRIVATE AriadShot::app AriadShot::options)
# qt_add_executable() links Qt6::Core itself.
ariadshot_architecture_register(TARGET ariadshot-daemon NAME app/main ALLOWED AriadShot::app Qt6::Core)

include(GNUInstallDirs)
install(TARGETS ariadshot-daemon RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
