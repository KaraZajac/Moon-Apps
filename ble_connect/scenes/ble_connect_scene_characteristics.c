#include "../ble_connect_app_i.h"

static void ble_connect_scene_chars_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void format_char_label(BleGattCharacteristic* chr, char* buf, size_t buf_size) {
    char props[16] = "";
    size_t p = 0;
    if(chr->properties & 0x02) props[p++] = 'R'; // Read
    if(chr->properties & 0x04) props[p++] = 'W'; // Write (no resp)
    if(chr->properties & 0x08) props[p++] = 'W'; // Write
    if(chr->properties & 0x10) props[p++] = 'N'; // Notify
    if(chr->properties & 0x20) props[p++] = 'I'; // Indicate
    props[p] = '\0';

    if(chr->uuid_type == 1) {
        const char* name = ble_connect_uuid_name(chr->uuid_16);
        if(name) {
            snprintf(buf, buf_size, "%s [%s]", name, props);
        } else {
            snprintf(buf, buf_size, "0x%04X [%s]", chr->uuid_16, props);
        }
    } else {
        const uint8_t* u = chr->uuid_128;
        snprintf(
            buf, buf_size, "%02X%02X..%02X%02X [%s]",
            u[15], u[14], u[1], u[0], props);
    }
}

void ble_connect_scene_characteristics_on_enter(void* context) {
    BleConnectApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Characteristics");

    if(app->char_count == 0) {
        submenu_add_item(submenu, "Discovering...", 0, NULL, NULL);
    } else {
        for(uint8_t i = 0; i < app->char_count; i++) {
            char label[48];
            format_char_label(&app->chars[i], label, sizeof(label));
            submenu_add_item(
                submenu, label, i,
                ble_connect_scene_chars_submenu_callback, app);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_characteristics_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectCustomEventCharsDiscovered) {
            // Refresh with discovered characteristics
            ble_connect_scene_characteristics_on_enter(app);
            consumed = true;
        } else if(event.event < app->char_count) {
            app->selected_char_idx = event.event;
            app->has_read_data = false;
            scene_manager_next_scene(app->scene_manager, BleConnectSceneCharDetail);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_characteristics_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
