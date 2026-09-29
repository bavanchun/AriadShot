# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Objective-C++ and Swift shims for Apple APIs. No code yet; OBJCXX, Swift and the Apple frameworks are enabled
# here with the first shim.
ariadshot_add_module(NAME backends/macos KIND INTERFACE ALLOWED AriadShot::platform)
target_link_libraries(ariadshot_backends_macos INTERFACE AriadShot::platform)
