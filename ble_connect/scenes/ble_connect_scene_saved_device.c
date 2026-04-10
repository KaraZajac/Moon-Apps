#include "../ble_connect_app_i.h"

enum {
    BleConnectSavedDeviceConnect,
    BleConnectSavedDeviceDelete,
};

static void ble_connect_scene_saved_device_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_connect_scene_saved_device_on_enter(void* context) {
    BleConnectApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);

    char header[48];
    snprintf(header, sizeof(header), "%s",
        app->current_device.has_name ? app->current_device.name : "Saved Device");
    submenu_set_header(submenu, header);

    submenu_add_item(
        submenu, "Connect", BleConnectSavedDeviceConnect,
        ble_connect_scene_saved_device_submenu_callback, app);
    submenu_add_item(
        submenu, "Delete", BleConnectSavedDeviceDelete,
        ble_connect_scene_saved_device_submenu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_saved_device_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectSavedDeviceConnect) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneConnecting);
            consumed = true;
        } else if(event.event == BleConnectSavedDeviceDelete) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneDeleteConfirm);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_saved_device_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
