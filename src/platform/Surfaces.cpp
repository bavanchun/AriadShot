// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "platform/SurfaceContent.h"
#include "platform/SurfaceHost.h"

namespace ariadshot::platform {

// The interfaces' destructors are defined here so that each vtable is emitted once, in this module.
HostedSurface::~HostedSurface() = default;
SurfaceHost::~SurfaceHost() = default;
SurfaceContent::~SurfaceContent() = default;

} // namespace ariadshot::platform
