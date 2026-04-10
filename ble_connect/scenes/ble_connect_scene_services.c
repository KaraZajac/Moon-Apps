#include "../ble_connect_app_i.h"

enum {
    BleConnectServicesSubmenuDisconnect = 0xFF,
    BleConnectServicesSubmenuSave = 0xFE,
};

static void ble_connect_scene_services_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_connect_scene_services_on_enter(void* context) {
    BleConnectApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "GATT Services");

    // Show loading if services not yet discovered
    if(app->service_count == 0) {
        submenu_add_item(submenu, "Discovering...", 0, NULL, NULL);
    } else {
        for(uint8_t i = 0; i < app->service_count; i++) {
            char label[40];
            if(app->services[i].uuid_type == 1) {
                const char* name = ble_connect_uuid_name(app->services[i].uuid_16);
                if(name) {
                    snprintf(label, sizeof(label), "%s", name);
                } else {
                    snprintf(label, sizeof(label), "0x%04X", app->services[i].uuid_16);
                }
            } else {
                // Show first 4 bytes of 128-bit UUID for identification
                // UUID is stored little-endian, bytes 12-15 are the most significant
                const uint8_t* u = app->services[i].uuid_128;
                snprintf(
                    label, sizeof(label), "%02X%02X%02X%02X-...",
                    u[15], u[14], u[13], u[12]);
            }
            submenu_add_item(
                submenu, label, i,
                ble_connect_scene_services_submenu_callback, app);
        }
    }

    submenu_add_item(
        submenu, "Save Device", BleConnectServicesSubmenuSave,
        ble_connect_scene_services_submenu_callback, app);
    submenu_add_item(
        submenu, "Disconnect", BleConnectServicesSubmenuDisconnect,
        ble_connect_scene_services_submenu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_services_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectCustomEventServicesDiscovered) {
            // Refresh the view with discovered services
            ble_connect_scene_services_on_enter(app);
            consumed = true;
        } else if(event.event == BleConnectServicesSubmenuDisconnect) {
            if(app->connected) {
                gap_disconnect(app->connection_handle);
                app->connected = false;
            }
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneStart);
            consumed = true;
        } else if(event.event == BleConnectServicesSubmenuSave) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneSaveName);
            consumed = true;
        } else if(event.event < app->service_count) {
            app->selected_service_idx = event.event;
            app->char_count = 0;
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[event.event]);
            scene_manager_next_scene(app->scene_manager, BleConnectSceneCharacteristics);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        // Back = disconnect and return to start
        if(app->connected) {
            gap_disconnect(app->connection_handle);
            app->connected = false;
        }
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, BleConnectSceneStart);
        consumed = true;
    }
    return consumed;
}

void ble_connect_scene_services_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
