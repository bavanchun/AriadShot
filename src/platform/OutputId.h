// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

namespace ariadshot::platform {

// Names one output. Qt's screens and AriadShot's own Wayland connection are matched by the wl_output name, so the
// value carries no platform handle (docs/spec/02-modules-and-interfaces.md §6). Thread: any.
struct OutputId {
    QString name;

    friend bool operator==(const OutputId&, const OutputId&) = default;
};

} // namespace ariadshot::platform
