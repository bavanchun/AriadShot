// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDebug>
#include <QMetaObject>

#include <atomic>
#include <backends/wayland/WaylandSession.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <latch>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

namespace ariadshot::backends::wayland {

struct WaylandSession::Impl {
    QString displayName;
    struct wl_display* display = nullptr;
    struct wl_event_queue* queue = nullptr;
    struct wl_registry* registry = nullptr;
    OutputRegistry outputRegistry;

    int wakePipe[2] = {-1, -1};
    std::thread thread;
    std::atomic<bool> isConnected{false};
    std::atomic<bool> isDispatching{false};
    std::atomic<bool> stopping{false};
    std::thread::id dispatchThreadId;
    WaylandSession* q = nullptr;

    void teardown(bool emitSignal) {
        const bool wasConnected = isConnected.exchange(false, std::memory_order_acq_rel);
        if (!wasConnected && !thread.joinable() && display == nullptr) {
            return;
        }

        stopping.store(true, std::memory_order_release);

        if (wakePipe[1] >= 0) {
            const char byte = 1;
            [[maybe_unused]] const auto written = write(wakePipe[1], &byte, sizeof(byte));
        }

        if (thread.joinable()) {
            thread.join();
        }

        outputRegistry.cleanup();

        if (registry != nullptr) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        if (queue != nullptr) {
            wl_event_queue_destroy(queue);
            queue = nullptr;
        }

        if (display != nullptr) {
            wl_display_disconnect(display);
            display = nullptr;
        }

        if (wakePipe[0] >= 0) {
            close(wakePipe[0]);
            wakePipe[0] = -1;
        }
        if (wakePipe[1] >= 0) {
            close(wakePipe[1]);
            wakePipe[1] = -1;
        }

        if (emitSignal && wasConnected) {
            Q_EMIT q->disconnected();
        }
    }

    void dispatchLoop() {
        // Thread: WaylandSession
        while (!stopping.load(std::memory_order_acquire)) {
            int prepRet = -1;
            while ((prepRet = wl_display_prepare_read_queue(display, queue)) != 0) {
                if (stopping.load(std::memory_order_acquire)) {
                    break;
                }
                if (wl_display_dispatch_queue_pending(display, queue) < 0 || wl_display_get_error(display) != 0) {
                    break;
                }
            }

            if (prepRet == 0 && (stopping.load(std::memory_order_acquire) || wl_display_get_error(display) != 0)) {
                wl_display_cancel_read(display);
                break;
            }
            if (prepRet != 0) {
                break;
            }

            auto displayEvents = static_cast<unsigned int>(POLLIN);
            while (wl_display_flush(display) < 0) {
                if (errno == EAGAIN) {
                    displayEvents |= static_cast<unsigned int>(POLLOUT);
                    break;
                }
                if (errno != EINTR) {
                    break;
                }
            }
            if (wl_display_get_error(display) != 0) {
                wl_display_cancel_read(display);
                break;
            }

            struct pollfd pfd[2];
            pfd[0].fd = wl_display_get_fd(display);
            pfd[0].events = static_cast<short>(displayEvents);
            pfd[0].revents = 0;
            pfd[1].fd = wakePipe[0];
            pfd[1].events = POLLIN;
            pfd[1].revents = 0;

            const int ret = poll(pfd, 2, -1);
            if (ret <= 0) {
                wl_display_cancel_read(display);
                if (ret < 0 && errno == EINTR) {
                    continue;
                }
                break;
            }

            const auto wakeRevents = static_cast<unsigned int>(pfd[1].revents);
            if ((wakeRevents & static_cast<unsigned int>(POLLIN)) != 0U) {
                wl_display_cancel_read(display);
                char buf[64];
                while (read(wakePipe[0], buf, sizeof(buf)) > 0) {
                }
                break;
            }

            const auto displayRevents = static_cast<unsigned int>(pfd[0].revents);
            constexpr auto kErrorOrInputMask = static_cast<unsigned int>(POLLIN) | static_cast<unsigned int>(POLLERR) |
                                               static_cast<unsigned int>(POLLHUP);
            if ((displayRevents & kErrorOrInputMask) != 0U) {
                if (wl_display_read_events(display) < 0) {
                    break;
                }
            } else {
                wl_display_cancel_read(display);
            }

            if (wl_display_dispatch_queue_pending(display, queue) < 0 || wl_display_get_error(display) != 0) {
                break;
            }
        }

        isDispatching.store(false, std::memory_order_release);
        if (!stopping.load(std::memory_order_acquire)) {
            if (isConnected.exchange(false, std::memory_order_acq_rel)) {
                QMetaObject::invokeMethod(q, "connectionLost", Qt::QueuedConnection);
            }
        }
    }
};

namespace {

void handleRegistryGlobal(void* data, struct wl_registry* reg, uint32_t id, const char* interface, uint32_t version) {
    auto* session = static_cast<WaylandSession*>(data);
    if (session != nullptr && std::strcmp(interface, "wl_output") == 0) {
        session->outputRegistry()->bindOutput(reg, id, version, session->queue());
    }
}

void handleRegistryGlobalRemove(void* data, struct wl_registry* /*reg*/, uint32_t id) {
    auto* session = static_cast<WaylandSession*>(data);
    if (session != nullptr) {
        session->outputRegistry()->removeOutput(id);
    }
}

const struct wl_registry_listener kRegistryListener = {
    .global = handleRegistryGlobal,
    .global_remove = handleRegistryGlobalRemove,
};

} // namespace

WaylandSession::WaylandSession(const QString& displayName, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->displayName = displayName;
    m_impl->q = this;
}

WaylandSession::~WaylandSession() { disconnectAndStop(); }

bool WaylandSession::connectAndStart() {
    // Thread: GUI
    if (m_impl->isConnected.load(std::memory_order_acquire)) {
        return true;
    }

    if (m_impl->thread.joinable() || m_impl->display != nullptr) {
        m_impl->teardown(/*emitSignal=*/false);
    }

    const QByteArray nameBytes = m_impl->displayName.toUtf8();
    const char* displayArg = m_impl->displayName.isEmpty() ? nullptr : nameBytes.constData();
    m_impl->display = wl_display_connect(displayArg);
    if (m_impl->display == nullptr) {
        return false;
    }

    m_impl->queue = wl_display_create_queue(m_impl->display);
    if (m_impl->queue == nullptr) {
        wl_display_disconnect(m_impl->display);
        m_impl->display = nullptr;
        return false;
    }

    if (pipe2(m_impl->wakePipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        wl_event_queue_destroy(m_impl->queue);
        wl_display_disconnect(m_impl->display);
        m_impl->display = nullptr;
        m_impl->queue = nullptr;
        return false;
    }

    m_impl->registry = wl_display_get_registry(m_impl->display);
    if (m_impl->registry == nullptr) {
        close(m_impl->wakePipe[0]);
        close(m_impl->wakePipe[1]);
        m_impl->wakePipe[0] = -1;
        m_impl->wakePipe[1] = -1;
        wl_event_queue_destroy(m_impl->queue);
        wl_display_disconnect(m_impl->display);
        m_impl->display = nullptr;
        m_impl->queue = nullptr;
        return false;
    }

    wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(m_impl->registry), m_impl->queue);
    wl_registry_add_listener(m_impl->registry, &kRegistryListener, this);

    m_impl->stopping.store(false, std::memory_order_release);
    m_impl->isConnected.store(true, std::memory_order_release);

    std::latch startedLatch{1};
    m_impl->thread = std::thread([this, &startedLatch]() {
        m_impl->dispatchThreadId = std::this_thread::get_id();
        m_impl->isDispatching.store(true, std::memory_order_release);
        startedLatch.count_down();
        m_impl->dispatchLoop();
    });

    startedLatch.wait();

    Q_EMIT connected();
    return true;
}

void WaylandSession::disconnectAndStop() {
    // Thread: GUI
    m_impl->teardown(/*emitSignal=*/true);
}

void WaylandSession::setDisplayName(const QString& displayName) {
    // Thread: GUI
    m_impl->displayName = displayName;
}

QString WaylandSession::displayName() const {
    // Thread: any
    return m_impl->displayName;
}

bool WaylandSession::isConnected() const { return m_impl->isConnected.load(std::memory_order_acquire); }

bool WaylandSession::isDispatching() const { return m_impl->isDispatching.load(std::memory_order_acquire); }

std::thread::id WaylandSession::dispatchThreadId() const { return m_impl->dispatchThreadId; }

OutputRegistry* WaylandSession::outputRegistry() const { return &m_impl->outputRegistry; }

struct wl_display* WaylandSession::display() const { return m_impl->display; }

struct wl_event_queue* WaylandSession::queue() const { return m_impl->queue; }

} // namespace ariadshot::backends::wayland
