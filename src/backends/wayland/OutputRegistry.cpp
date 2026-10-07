// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDebug>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <backends/wayland/OutputRegistry.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

namespace ariadshot::backends::wayland {

QSize WaylandOutputInfo::bufferDerivedSize() const {
    const int effectiveScale = scale > 0 ? scale : 1;
    int w = currentModeSize.width() / effectiveScale;
    int h = currentModeSize.height() / effectiveScale;

    // Sway / Wayland transform constants:
    // WL_OUTPUT_TRANSFORM_90 = 1, WL_OUTPUT_TRANSFORM_270 = 3,
    // WL_OUTPUT_TRANSFORM_FLIPPED_90 = 5, WL_OUTPUT_TRANSFORM_FLIPPED_270 = 7.
    if (transform == WL_OUTPUT_TRANSFORM_90 || transform == WL_OUTPUT_TRANSFORM_270 ||
        transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
        std::swap(w, h);
    }
    return {w, h};
}

namespace {

struct OutputTracker {
    OutputRegistry* registry = nullptr;
    uint32_t id = 0;
    struct wl_output* output = nullptr;
    WaylandOutputInfo pending;
};

void destroyOutputProxy(struct wl_output* output) {
    if (output == nullptr) {
        return;
    }
    if (wl_output_get_version(output) >= 3) {
        wl_output_release(output);
    } else {
        wl_output_destroy(output);
    }
}

} // namespace

struct OutputRegistry::Impl {
    // Only mutated on WaylandSession dispatch thread:
    std::unordered_map<uint32_t, std::unique_ptr<OutputTracker>> trackersById;

    // Thread-safe state queried by external callers:
    mutable std::mutex mutex;
    QHash<uint32_t, WaylandOutputInfo> outputsById;
    OutputRegistry* q = nullptr;

    void updateOutput(uint32_t id, const WaylandOutputInfo& info, bool* isNew) {
        std::scoped_lock lock(mutex);
        if (isNew != nullptr) {
            *isNew = !outputsById.contains(id);
        }
        outputsById.insert(id, info);
    }
};

namespace {

void handleGeometry(void* data, struct wl_output* /*output*/, int32_t /*x*/, int32_t /*y*/, int32_t physicalWidth,
                    int32_t physicalHeight, int32_t subpixel, const char* make, const char* model, int32_t transform) {
    auto* tracker = static_cast<OutputTracker*>(data);
    tracker->pending.physicalSizeMm = QSize(physicalWidth, physicalHeight);
    tracker->pending.subpixel = subpixel;
    tracker->pending.make = QString::fromUtf8(make);
    tracker->pending.model = QString::fromUtf8(model);
    tracker->pending.transform = transform;
}

void handleMode(void* data, struct wl_output* /*output*/, uint32_t flags, int32_t width, int32_t height,
                int32_t refresh) {
    auto* tracker = static_cast<OutputTracker*>(data);
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
        tracker->pending.currentModeSize = QSize(width, height);
        tracker->pending.refreshRate = refresh;
    }
}

void handleDone(void* data, struct wl_output* /*output*/) {
    auto* tracker = static_cast<OutputTracker*>(data);
    tracker->pending.done = true;

    const WaylandOutputInfo copy = tracker->pending;
    OutputRegistry* reg = tracker->registry;
    if (reg != nullptr) {
        reg->handleOutputDone(tracker->id, copy);
    }
}

void handleScale(void* data, struct wl_output* /*output*/, int32_t factor) {
    auto* tracker = static_cast<OutputTracker*>(data);
    tracker->pending.scale = factor > 0 ? factor : 1;
}

void handleName(void* data, struct wl_output* /*output*/, const char* name) {
    auto* tracker = static_cast<OutputTracker*>(data);
    tracker->pending.name = QString::fromUtf8(name);
}

void handleDescription(void* data, struct wl_output* /*output*/, const char* description) {
    auto* tracker = static_cast<OutputTracker*>(data);
    tracker->pending.description = QString::fromUtf8(description);
}

const struct wl_output_listener kOutputListener = {
    .geometry = handleGeometry,
    .mode = handleMode,
    .done = handleDone,
    .scale = handleScale,
    .name = handleName,
    .description = handleDescription,
};

} // namespace

OutputRegistry::OutputRegistry(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->q = this;
}

OutputRegistry::~OutputRegistry() { cleanup(); }

void OutputRegistry::handleOutputDone(uint32_t id, const WaylandOutputInfo& info) {
    bool isNew = false;
    m_impl->updateOutput(id, info, &isNew);
    if (isNew) {
        Q_EMIT outputAdded(info);
    } else {
        Q_EMIT outputChanged(info);
    }
}

OutputMatchResult OutputRegistry::matchOutputs(const QList<WaylandOutputInfo>& outputs,
                                               const QStringList& screenNames) {
    OutputMatchResult result;
    QSet<QString> remainingScreens(screenNames.begin(), screenNames.end());

    for (const auto& out : outputs) {
        if (!out.name.isEmpty() && remainingScreens.contains(out.name)) {
            result.matched.append(out);
            remainingScreens.remove(out.name);
        } else {
            result.unmatchedOutputs.append(out);
        }
    }

    for (const auto& name : screenNames) {
        if (remainingScreens.contains(name)) {
            result.unmatchedScreenNames.append(name);
        }
    }

    return result;
}

void OutputRegistry::bindOutput(struct wl_registry* registry, uint32_t id, uint32_t version,
                                struct wl_event_queue* queue) {
    const uint32_t bindVersion = std::min<uint32_t>(version, 4U);
    auto* output = static_cast<struct wl_output*>(wl_registry_bind(registry, id, &wl_output_interface, bindVersion));
    if (output == nullptr) {
        return;
    }

    if (queue != nullptr) {
        wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(output), queue);
    }

    auto tracker = std::make_unique<OutputTracker>();
    tracker->registry = this;
    tracker->id = id;
    tracker->output = output;
    tracker->pending.id = id;

    auto* trackerPtr = tracker.get();
    m_impl->trackersById[id] = std::move(tracker);

    wl_output_add_listener(output, &kOutputListener, trackerPtr);
}

void OutputRegistry::removeOutput(uint32_t id) {
    auto trackerIt = m_impl->trackersById.find(id);
    if (trackerIt != m_impl->trackersById.end()) {
        destroyOutputProxy(trackerIt->second->output);
        trackerIt->second->output = nullptr;
        m_impl->trackersById.erase(trackerIt);
    }

    WaylandOutputInfo removed;
    {
        std::scoped_lock lock(m_impl->mutex);
        auto it = m_impl->outputsById.find(id);
        if (it != m_impl->outputsById.end()) {
            removed = it.value();
            m_impl->outputsById.erase(it);
        }
    }

    if (removed.id != 0) {
        Q_EMIT outputRemoved(removed.id, removed.name);
    }
}

void OutputRegistry::cleanup() {
    for (auto& [id, tracker] : m_impl->trackersById) {
        if (tracker && tracker->output != nullptr) {
            destroyOutputProxy(tracker->output);
            tracker->output = nullptr;
        }
    }
    m_impl->trackersById.clear();

    {
        std::scoped_lock lock(m_impl->mutex);
        m_impl->outputsById.clear();
    }
}

struct wl_output* OutputRegistry::outputProxy(uint32_t id) const {
    auto it = m_impl->trackersById.find(id);
    if (it != m_impl->trackersById.end() && it->second != nullptr) {
        return it->second->output;
    }
    return nullptr;
}

QList<WaylandOutputInfo> OutputRegistry::outputs() const {
    std::scoped_lock lock(m_impl->mutex);
    return m_impl->outputsById.values();
}

WaylandOutputInfo OutputRegistry::findByName(const QString& name) const {
    std::scoped_lock lock(m_impl->mutex);
    for (const auto& out : m_impl->outputsById) {
        if (out.name == name) {
            return out;
        }
    }
    return {};
}

OutputMatchResult OutputRegistry::matchScreens(const QStringList& screenNames) const {
    const OutputMatchResult result = matchOutputs(outputs(), screenNames);
    for (const auto& unmatched : result.unmatchedOutputs) {
        // spec 04 §3 rule 3: "An output that cannot be matched is not captured, and the failure is logged at warning
        // level."
        qWarning().noquote() << "Wayland output" << unmatched.name
                             << "cannot be matched to any Qt screen; capture will skip it";
    }
    return result;
}

} // namespace ariadshot::backends::wayland
