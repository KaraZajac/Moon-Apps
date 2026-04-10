#include "../ble_connect_app_i.h"

enum {
    BleConnectCharDetailRead,
    BleConnectCharDetailWrite,
    BleConnectCharDetailSubscribe,
};

static void ble_connect_scene_char_detail_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void build_detail_view(BleConnectApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    BleGattCharacteristic* chr = &app->chars[app->selected_char_idx];

    // Header with UUID
    char header[32];
    if(chr->uuid_type == 1) {
        const char* name = ble_connect_uuid_name(chr->uuid_16);
        if(name) {
            snprintf(header, sizeof(header), "%s", name);
        } else {
            snprintf(header, sizeof(header), "Char 0x%04X", chr->uuid_16);
        }
    } else {
        snprintf(header, sizeof(header), "Characteristic");
    }
    submenu_set_header(submenu, header);

    // Show current value if we have it
    if(app->has_read_data && app->read_len > 0) {
        char val_str[64] = "Val: ";
        size_t pos = 5;
        for(uint16_t i = 0; i < app->read_len && pos < sizeof(val_str) - 4; i++) {
            snprintf(val_str + pos, sizeof(val_str) - pos, "%02X ", app->read_buf[i]);
            pos += 3;
        }
        submenu_add_item(submenu, val_str, 0xFF, NULL, NULL);
    }

    // Action buttons based on properties
    if(chr->properties & 0x02) { // Read
        submenu_add_item(
            submenu, "Read Value", BleConnectCharDetailRead,
            ble_connect_scene_char_detail_submenu_callback, app);
    }
    if(chr->properties & (0x04 | 0x08)) { // Write
        submenu_add_item(
            submenu, "Write Value", BleConnectCharDetailWrite,
            ble_connect_scene_char_detail_submenu_callback, app);
    }
    if(chr->properties & 0x10) { // Notify
        submenu_add_item(
            submenu, "Toggle Notifications", BleConnectCharDetailSubscribe,
            ble_connect_scene_char_detail_submenu_callback, app);
    }
}

void ble_connect_scene_char_detail_on_enter(void* context) {
    BleConnectApp* app = context;
    build_detail_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_char_detail_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        BleGattCharacteristic* chr = &app->chars[app->selected_char_idx];

        if(event.event == BleConnectCharDetailRead) {
            ble_gatt_client_read(app->connection_handle, chr->value_handle);
            consumed = true;
        } else if(event.event == BleConnectCharDetailWrite) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneCharWrite);
            consumed = true;
        } else if(event.event == BleConnectCharDetailSubscribe) {
            // Toggle — we don't track state, just send enable
            ble_gatt_client_subscribe_notifications(
                app->connection_handle, chr->value_handle, true);
            consumed = true;
        } else if(event.event == BleConnectCustomEventReadComplete ||
                  event.event == BleConnectCustomEventNotification) {
            // Refresh view with new data
            build_detail_view(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_char_detail_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
