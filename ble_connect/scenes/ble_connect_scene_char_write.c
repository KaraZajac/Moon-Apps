#include "../ble_connect_app_i.h"

static void ble_connect_scene_char_write_callback(void* context) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, BleConnectCustomEventWriteComplete);
}

void ble_connect_scene_char_write_on_enter(void* context) {
    BleConnectApp* app = context;

    memset(app->byte_store, 0, sizeof(app->byte_store));
    app->byte_store_len = 1; // Start with 1 byte

    byte_input_set_result_callback(
        app->byte_input,
        ble_connect_scene_char_write_callback,
        NULL,
        app,
        app->byte_store,
        app->byte_store_len);
    byte_input_set_header_text(app->byte_input, "Enter value (hex)");

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewByteInput);
}

bool ble_connect_scene_char_write_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectCustomEventWriteComplete) {
            BleGattCharacteristic* chr = &app->chars[app->selected_char_idx];
            ble_gatt_client_write(
                app->connection_handle,
                chr->value_handle,
                app->byte_store,
                app->byte_store_len);
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_char_write_on_exit(void* context) {
    BleConnectApp* app = context;
    UNUSED(app);
}
