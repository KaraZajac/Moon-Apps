#include "../ble_connect_app_i.h"

enum {
    BleConnectStartSubmenuScan,
    BleConnectStartSubmenuSaved,
};

static void ble_connect_scene_start_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_connect_scene_start_on_enter(void* context) {
    BleConnectApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "BLE Connect");
    submenu_add_item(
        submenu, "Scan for Devices", BleConnectStartSubmenuScan,
        ble_connect_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Saved Devices", BleConnectStartSubmenuSaved,
        ble_connect_scene_start_submenu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_start_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectStartSubmenuScan) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneScan);
            consumed = true;
        } else if(event.event == BleConnectStartSubmenuSaved) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneSavedList);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_start_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
