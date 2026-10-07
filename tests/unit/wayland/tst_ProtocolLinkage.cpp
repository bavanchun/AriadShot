// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include <cstring>
#include <wayland-client.h>

// NOLINTBEGIN(readability-identifier-naming)
extern "C" {
extern const struct wl_interface ext_image_copy_capture_manager_v1_interface;
extern const struct wl_interface ext_output_image_capture_source_manager_v1_interface;
extern const struct wl_interface ext_foreign_toplevel_image_capture_source_manager_v1_interface;
extern const struct wl_interface ext_data_control_manager_v1_interface;
extern const struct wl_interface ext_foreign_toplevel_list_v1_interface;
extern const struct wl_interface ext_foreign_toplevel_handle_v1_interface;
extern const struct wl_interface zwlr_screencopy_manager_v1_interface;
extern const struct wl_interface zwlr_data_control_manager_v1_interface;
extern const struct wl_interface zwlr_virtual_pointer_manager_v1_interface;
extern const struct wl_interface zwlr_layer_shell_v1_interface;
extern const struct wl_interface zwlr_foreign_toplevel_manager_v1_interface;
extern const struct wl_interface zwlr_foreign_toplevel_handle_v1_interface;
extern const struct wl_interface hyprland_toplevel_export_manager_v1_interface;
extern const struct wl_interface hyprland_global_shortcuts_manager_v1_interface;
extern const struct wl_interface xdg_wm_base_interface;
extern const struct wl_interface xdg_popup_interface;
}
// NOLINTEND(readability-identifier-naming)

class ProtocolLinkageTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void testManagerInterfacesLinkable();
};

void ProtocolLinkageTest::testManagerInterfacesLinkable() {
    const struct wl_interface* interfaces[] = {
        &ext_image_copy_capture_manager_v1_interface,
        &ext_output_image_capture_source_manager_v1_interface,
        &ext_foreign_toplevel_image_capture_source_manager_v1_interface,
        &ext_data_control_manager_v1_interface,
        &ext_foreign_toplevel_list_v1_interface,
        &ext_foreign_toplevel_handle_v1_interface,
        &zwlr_screencopy_manager_v1_interface,
        &zwlr_data_control_manager_v1_interface,
        &zwlr_virtual_pointer_manager_v1_interface,
        &zwlr_layer_shell_v1_interface,
        &zwlr_foreign_toplevel_manager_v1_interface,
        &zwlr_foreign_toplevel_handle_v1_interface,
        &hyprland_toplevel_export_manager_v1_interface,
        &hyprland_global_shortcuts_manager_v1_interface,
        &xdg_wm_base_interface,
        &xdg_popup_interface,
    };

    for (const auto* iface : interfaces) {
        if (iface == nullptr || iface->name == nullptr) {
            QFAIL("interface or name is null");
        }
        QVERIFY(std::strlen(iface->name) > 0);
    }
}

QTEST_GUILESS_MAIN(ProtocolLinkageTest)
#include "tst_ProtocolLinkage.moc"
