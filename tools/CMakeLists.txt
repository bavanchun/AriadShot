# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

add_subdirectory(imgdiff)
if(ARIADSHOT_MEDIA)
    add_subdirectory(bench)
endif()
