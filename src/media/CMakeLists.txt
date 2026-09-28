# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Recorder and studio. No code yet; FFmpeg, PipeWire and Qt6::GuiPrivate are found here, behind options that
# default to OFF, when the first media code lands.
ariadshot_add_module(NAME media KIND INTERFACE ALLOWED AriadShot::core AriadShot::render Qt6::GuiPrivate)
target_link_libraries(ariadshot_media INTERFACE AriadShot::core AriadShot::render)
