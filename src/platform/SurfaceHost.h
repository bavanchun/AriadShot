// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "platform/OutputId.h"

#include <QtGlobal>

#include <cstdint>
#include <functional>
#include <memory>

namespace ariadshot::platform {

class SurfaceContent;

// How a surface takes the keyboard: not at all, when the user focuses it, or exclusively (an overlay holding the
// selection grabs every key until it is dismissed).
enum class KeyboardInteractivity : std::uint8_t { None, OnDemand, Exclusive };

// One platform surface made by a SurfaceHost. It exists from create() until it is destroyed, and is either mapped or
// dormant; destroying it tears the surface down. Thread: GUI.
class HostedSurface {
  public:
    HostedSurface() = default;
    virtual ~HostedSurface();
    Q_DISABLE_COPY_MOVE(HostedSurface)

    // Map or unmap the surface. Both return at once; the handler reports when the change has happened.
    virtual void map() = 0;
    virtual void unmap() = 0;
    [[nodiscard]] virtual bool isMapped() const = 0;

    // Runs on the GUI thread each time the surface becomes mapped (true) or unmapped (false).
    virtual void setMappedHandler(std::function<void(bool mapped)> handler) = 0;
};

// Maps platform surfaces and forwards their events to the content they show. The host is the only host-specific piece
// of an overlay (docs/spec/04-capture-and-overlay.md §5.1). Thread: GUI.
class SurfaceHost {
  public:
    enum class Role : std::uint8_t {
        Overlay,
        Thumbnail,
        Pin,
        Toast,
        HistoryPanel,
        Countdown,
        RecordingHud,
        WebcamBubble,
        ScrollPreview
    };

    SurfaceHost() = default;
    virtual ~SurfaceHost();
    Q_DISABLE_COPY_MOVE(SurfaceHost)

    // Returns at once and never blocks; the content must outlive the returned surface.
    virtual std::unique_ptr<HostedSurface> create(Role role, OutputId output, SurfaceContent& content) = 0;
};

} // namespace ariadshot::platform
