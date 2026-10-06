// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDebug>
#include <QMap>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

using namespace Qt::StringLiterals;

namespace {

struct OutputInfo {
    uint32_t id = 0;
    QString name;
    int32_t transform = -1;
    bool done = false;
    struct wl_output* output = nullptr;
};

struct RegistryContext {
    QMap<QString, uint32_t> globals;
    QVector<OutputInfo> outputs;
};

void registryGlobal(void* data, struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
    auto* ctx = static_cast<RegistryContext*>(data);
    const QString interfaceName = QString::fromUtf8(interface);
    ctx->globals.insert(interfaceName, version);

    if (interfaceName == u"wl_output"_s) {
        OutputInfo info;
        info.id = id;
        info.output = static_cast<struct wl_output*>(
            wl_registry_bind(registry, id, &wl_output_interface, std::min<uint32_t>(version, 4U)));
        ctx->outputs.append(info);
    }
}

void registryGlobalRemove(void* /*data*/, struct wl_registry* /*registry*/, uint32_t /*id*/) {}

const struct wl_registry_listener kRegistryListener = {
    .global = registryGlobal,
    .global_remove = registryGlobalRemove,
};

void outputGeometry(void* data, struct wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*,
                    const char*, int32_t transform) {
    static_cast<OutputInfo*>(data)->transform = transform;
}
void outputMode(void*, struct wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void outputDone(void* data, struct wl_output*) { static_cast<OutputInfo*>(data)->done = true; }
void outputScale(void*, struct wl_output*, int32_t) {}
void outputName(void* data, struct wl_output*, const char* name) {
    static_cast<OutputInfo*>(data)->name = QString::fromUtf8(name);
}
void outputDescription(void*, struct wl_output*, const char*) {}

const struct wl_output_listener kOutputListener = {
    .geometry = outputGeometry,
    .mode = outputMode,
    .done = outputDone,
    .scale = outputScale,
    .name = outputName,
    .description = outputDescription,
};

void cleanupOutputs(QVector<OutputInfo>& outputs) {
    for (auto& out : outputs) {
        if (out.output != nullptr) {
            wl_output_destroy(std::exchange(out.output, nullptr));
        }
    }
}

} // namespace

class ProtocolInventoryTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void inventoryAdvertisesRequiredGlobalsAndOutputs();
};

void ProtocolInventoryTest::inventoryAdvertisesRequiredGlobalsAndOutputs() {
    const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    if (waylandDisplay.isEmpty()) {
        QFAIL("WAYLAND_DISPLAY is not set or empty; headless compositor is required");
    }

    struct wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        QFAIL(qPrintable(u"Failed to connect to Wayland display '%1'"_s.arg(QString::fromLocal8Bit(waylandDisplay))));
    }

    RegistryContext ctx;
    struct wl_registry* registry = wl_display_get_registry(display);
    if (registry == nullptr) {
        wl_display_disconnect(display);
        QFAIL("Failed to acquire wl_registry from Wayland display");
    }

    const auto cleanup = qScopeGuard([&] {
        cleanupOutputs(ctx.outputs);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
    });

    wl_registry_add_listener(registry, &kRegistryListener, &ctx);
    if (wl_display_roundtrip(display) < 0) {
        QFAIL("wl_display_roundtrip failed during registry enumeration");
    }

    for (auto& out : ctx.outputs) {
        if (out.output != nullptr) {
            wl_output_add_listener(out.output, &kOutputListener, &out);
        }
    }

    if (wl_display_roundtrip(display) < 0) {
        QFAIL("wl_display_roundtrip failed during output enumeration");
    }

    qInfo() << "Wayland registry dump (" << ctx.globals.size() << "globals):";
    for (auto it = ctx.globals.cbegin(); it != ctx.globals.cend(); ++it) {
        qInfo().noquote() << " " << it.key() << "version:" << it.value();
    }

    struct RequiredGlobal {
        QString name;
        uint32_t minVersion;
    };

    const QVector<RequiredGlobal> requiredGlobals = {
        {.name = u"zwlr_layer_shell_v1"_s, .minVersion = 1},
        {.name = u"ext_image_copy_capture_manager_v1"_s, .minVersion = 1},
        {.name = u"ext_output_image_capture_source_manager_v1"_s, .minVersion = 1},
        {.name = u"zwlr_screencopy_manager_v1"_s, .minVersion = 1},
        {.name = u"wl_output"_s, .minVersion = 4},
    };

    QStringList missing;
    for (const auto& req : requiredGlobals) {
        const uint32_t version = ctx.globals.value(req.name, 0);
        if (version == 0) {
            missing.append(req.name);
        } else if (version < req.minVersion) {
            missing.append(u"%1 (v%2 < min v%3)"_s.arg(req.name).arg(version).arg(req.minVersion));
        }
    }

    const bool hasDataControl = (ctx.globals.value(u"ext_data_control_manager_v1"_s, 0) >= 1) ||
                                (ctx.globals.value(u"zwlr_data_control_manager_v1"_s, 0) >= 1);
    if (!hasDataControl) {
        missing.append(u"ext_data_control_manager_v1 (>= 1) or zwlr_data_control_manager_v1 (>= 1)"_s);
    }

    if (!missing.isEmpty()) {
        QFAIL(qPrintable(u"Missing required Wayland protocol global(s): %1"_s.arg(missing.join(u", "_s))));
    }

    QCOMPARE(ctx.outputs.size(), 2);
    bool hasHeadless1 = false;
    bool hasHeadless2 = false;
    for (const auto& out : ctx.outputs) {
        QVERIFY(!out.name.isEmpty());
        QVERIFY(out.done);
        if (out.name == u"HEADLESS-1"_s) {
            QCOMPARE(out.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_NORMAL));
            hasHeadless1 = true;
        } else if (out.name == u"HEADLESS-2"_s) {
            // Sway config uses clockwise transform angles, whereas Wayland's enum wl_output_transform
            // specifies counter-clockwise rotation. Sway inverts the angle in sway/commands/output/transform.c:60-64
            // (invert_rotation_direction()), so configuring "transform 90" produces WL_OUTPUT_TRANSFORM_270.
            QCOMPARE(out.transform, static_cast<int32_t>(WL_OUTPUT_TRANSFORM_270));
            hasHeadless2 = true;
        }
    }

    QVERIFY2(hasHeadless1, "Expected output named HEADLESS-1 with WL_OUTPUT_TRANSFORM_NORMAL");
    QVERIFY2(hasHeadless2, "Expected output named HEADLESS-2 with WL_OUTPUT_TRANSFORM_270");
}

QTEST_GUILESS_MAIN(ProtocolInventoryTest)
#include "tst_ProtocolInventory.moc"
