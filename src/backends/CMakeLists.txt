# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Platform backends. Each directory is a module; which ones exist depends on the target platform.
if(APPLE)
    add_subdirectory(macos)
else()
    add_subdirectory(wayland)
    add_subdirectory(portal)
    add_subdirectory(x11)
endif()
