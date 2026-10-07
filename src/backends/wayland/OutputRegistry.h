// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>

struct wl_output;
struct wl_registry;
struct wl_event_queue;

namespace ariadshot::backends::wayland {

// Thread: any for value type; pure helpers.
// Tracks metadata and geometry for one Wayland output (wl_output v4).
struct WaylandOutputInfo {
    uint32_t id = 0;
    QString name;
    QString description;
    QSize physicalSizeMm{0, 0};
    int32_t subpixel = 0;
    QString make;
    QString model;
    int32_t transform = 0; // enum wl_output_transform
    int32_t scale = 1;     // wl_output integer scale factor
    QSize currentModeSize{0, 0};
    int32_t refreshRate = 0; // mHz
    bool done = false;

    // Buffer-derived size: derived from currentModeSize, transform and integer scale factor.
    // If transform swaps axes (90 or 270 deg rotation), width and height are transposed.
    // Note: this represents buffer-space dimensions divided by integer scale, NOT the compositor's
    // logical desktop layout or output coordinates (which requires xdg-output or fractional-scale
    // protocol extensions planned for later phases).
    [[nodiscard]] QSize bufferDerivedSize() const;

    friend bool operator==(const WaylandOutputInfo&, const WaylandOutputInfo&) = default;
};

// Result of matching Wayland outputs against Qt screen names (docs/spec/04-capture-and-overlay.md §3 rule 3).
struct OutputMatchResult {
    QList<WaylandOutputInfo> matched;
    QList<WaylandOutputInfo> unmatchedOutputs;
    QStringList unmatchedScreenNames;
};

// Thread: WaylandSession thread for wl_output listener callbacks and mutations;
// thread-safe queries (outputs, findByName, matchScreens) can be called from GUI thread.
class OutputRegistry : public QObject {
    Q_OBJECT

  public:
    explicit OutputRegistry(QObject* parent = nullptr);
    ~OutputRegistry() override;

    // Pure matching logic: matches wl_output.name (v4) against Qt screen names (spec 04 §3).
    // Thread: any.
    static OutputMatchResult matchOutputs(const QList<WaylandOutputInfo>& outputs, const QStringList& screenNames);

    // Binds a wl_output global advertised by the registry.
    // Thread: WaylandSession.
    void bindOutput(struct wl_registry* registry, uint32_t id, uint32_t version, struct wl_event_queue* queue);

    // Removes an output global destroyed by the compositor.
    // Thread: WaylandSession.
    void removeOutput(uint32_t id);

    // Cleanly destroys and unbinds all tracked wl_output proxies.
    // Thread: GUI (after join) or WaylandSession.
    void cleanup();

    // Look up the raw wl_output proxy for a given global ID.
    // Thread: WaylandSession.
    [[nodiscard]] struct wl_output* outputProxy(uint32_t id) const;

    // Thread-safe access to tracked outputs.
    // Thread: any.
    [[nodiscard]] QList<WaylandOutputInfo> outputs() const;
    [[nodiscard]] WaylandOutputInfo findByName(const QString& name) const;
    [[nodiscard]] OutputMatchResult matchScreens(const QStringList& screenNames) const;

    // Internal handler invoked by wl_output listener on the dispatch thread.
    void handleOutputDone(uint32_t id, const WaylandOutputInfo& info);

  Q_SIGNALS:
    // Thread: WaylandSession (connect with Qt::QueuedConnection for GUI thread delivery).
    void outputAdded(const ariadshot::backends::wayland::WaylandOutputInfo& info);
    void outputChanged(const ariadshot::backends::wayland::WaylandOutputInfo& info);
    void outputRemoved(uint32_t id, const QString& name);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ariadshot::backends::wayland

Q_DECLARE_METATYPE(ariadshot::backends::wayland::WaylandOutputInfo)
