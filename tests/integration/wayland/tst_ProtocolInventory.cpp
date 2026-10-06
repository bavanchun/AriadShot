// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDebug>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

using namespace Qt::StringLiterals;

namespace {

struct OutputInfo {
    uint32_t id = 0;
    QString name;
    int32_t transform = -1, scale = 1, width = 0, height = 0;
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

void outputGeometry(void* data, struct wl_output* /*wlOutput*/, int32_t /*x*/, int32_t /*y*/, int32_t /*physicalWidth*/,
                    int32_t /*physicalHeight*/, int32_t /*subpixel*/, const char* /*make*/, const char* /*model*/,
                    int32_t transform) {
    static_cast<OutputInfo*>(data)->transform = transform;
}

void outputMode(void* data, struct wl_output* /*wlOutput*/, uint32_t flags, int32_t width, int32_t height,
                int32_t /*refresh*/) {
    if ((flags & static_cast<uint32_t>(WL_OUTPUT_MODE_CURRENT)) != 0U) {
        auto* info = static_cast<OutputInfo*>(data);
        info->width = width;
        info->height = height;
    }
}

void outputDone(void* data, struct wl_output* /*wlOutput*/) { static_cast<OutputInfo*>(data)->done = true; }

void outputScale(void* data, struct wl_output* /*wlOutput*/, int32_t factor) {
    static_cast<OutputInfo*>(data)->scale = factor;
}

void outputName(void* data, struct wl_output* /*wlOutput*/, const char* name) {
    static_cast<OutputInfo*>(data)->name = QString::fromUtf8(name);
}

void outputDescription(void* /*data*/, struct wl_output* /*wlOutput*/, const char* /*description*/) {}

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
            wl_output_destroy(out.output);
            out.output = nullptr;
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

    wl_registry_add_listener(registry, &kRegistryListener, &ctx);
    if (wl_display_roundtrip(display) < 0) {
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        QFAIL("wl_display_roundtrip failed during registry enumeration");
    }

    for (auto& out : ctx.outputs) {
        if (out.output != nullptr) {
            wl_output_add_listener(out.output, &kOutputListener, &out);
        }
    }

    if (wl_display_roundtrip(display) < 0) {
        cleanupOutputs(ctx.outputs);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        QFAIL("wl_display_roundtrip failed during output enumeration");
    }

    qInfo() << "Wayland registry dump (" << ctx.globals.size() << "globals):";
    for (auto it = ctx.globals.cbegin(); it != ctx.globals.cend(); ++it) {
        qInfo().noquote() << " " << it.key() << "version:" << it.value();
    }

    const QStringList requiredGlobals = {u"zwlr_layer_shell_v1"_s, u"ext_image_copy_capture_manager_v1"_s,
                                         u"ext_output_image_capture_source_manager_v1"_s,
                                         u"zwlr_screencopy_manager_v1"_s};

    QStringList missing;
    for (const auto& req : requiredGlobals) {
        if (!ctx.globals.contains(req)) {
            missing.append(req);
        }
    }

    if (!ctx.globals.contains(u"ext_data_control_manager_v1"_s) &&
        !ctx.globals.contains(u"zwlr_data_control_manager_v1"_s)) {
        missing.append(u"ext_data_control_manager_v1 or zwlr_data_control_manager_v1"_s);
    }

    if (!missing.isEmpty()) {
        cleanupOutputs(ctx.outputs);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        QFAIL(qPrintable(u"Missing required Wayland protocol global(s): %1"_s.arg(missing.join(u", "_s))));
    }

    QCOMPARE(ctx.outputs.size(), 2);
    bool hasNormal = false;
    bool hasTransform90 = false;
    for (const auto& out : ctx.outputs) {
        QVERIFY(!out.name.isEmpty());
        QVERIFY(out.done);
        if (out.transform == WL_OUTPUT_TRANSFORM_NORMAL) {
            hasNormal = true;
        } else if (out.transform == WL_OUTPUT_TRANSFORM_90 || out.transform == WL_OUTPUT_TRANSFORM_270) {
            hasTransform90 = true;
        }
    }

    cleanupOutputs(ctx.outputs);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    QVERIFY2(hasNormal, "Expected one output with normal orientation (WL_OUTPUT_TRANSFORM_NORMAL)");
    QVERIFY2(hasTransform90, "Expected one output with 90-degree transform");
}

QTEST_GUILESS_MAIN(ProtocolInventoryTest)
#include "tst_ProtocolInventory.moc"
