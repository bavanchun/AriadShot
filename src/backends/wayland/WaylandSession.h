// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QString>

#include <backends/wayland/OutputRegistry.h>
#include <memory>
#include <thread>
#include <wayland-client.h>

namespace ariadshot::backends::wayland {

// Thread: GUI creates, configures and manages lifecycle; events dispatch on dedicated session thread.
// Spec 04 §3: AriadShot's own secondary Wayland connection. Uses no Qt GUI classes.
class WaylandSession : public QObject {
    Q_OBJECT

  public:
    // Thread: GUI.
    explicit WaylandSession(const QString& displayName = {}, QObject* parent = nullptr);
    ~WaylandSession() override;

    // Connects to the Wayland display and starts the dedicated dispatch thread.
    // Thread: GUI.
    bool connectAndStart();

    // Signals the dispatch thread via wake-up pipe, joins the thread, and cleanly disconnects.
    // Thread: GUI.
    void disconnectAndStop();

    // Status queries
    // Thread: any.
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isDispatching() const;
    [[nodiscard]] std::thread::id dispatchThreadId() const;

    // Output registry tracking wl_output globals
    // Thread: any.
    [[nodiscard]] OutputRegistry* outputRegistry() const;

    // Low-level connection handles
    // Thread: WaylandSession.
    [[nodiscard]] struct wl_display* display() const;
    [[nodiscard]] struct wl_event_queue* queue() const;

  Q_SIGNALS:
    // Thread: GUI (dispatched via Qt::QueuedConnection).
    void connected();
    void disconnected();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ariadshot::backends::wayland
