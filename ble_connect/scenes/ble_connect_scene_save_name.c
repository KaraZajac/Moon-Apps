#include "../ble_connect_app_i.h"

static void ble_connect_scene_save_name_callback(void* context) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void ble_connect_scene_save_name_on_enter(void* context) {
    BleConnectApp* app = context;

    // Pre-fill with device name if available
    if(app->current_device.has_name && app->current_device.name[0]) {
        strlcpy(app->text_store, app->current_device.name, sizeof(app->text_store));
    } else {
        char mac[18];
        ble_connect_device_mac_to_str(app->current_device.address, mac, sizeof(mac));
        strlcpy(app->text_store, mac, sizeof(app->text_store));
    }

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Device Name:");
    text_input_set_result_callback(
        app->text_input,
        ble_connect_scene_save_name_callback,
        app,
        app->text_store,
        sizeof(app->text_store),
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewTextInput);
}

bool ble_connect_scene_save_name_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        // Save the device
        furi_string_printf(
            app->file_path, "%s/%s%s",
            BLE_CONNECT_APP_FOLDER, app->text_store, BLE_CONNECT_APP_EXTENSION);

        // Copy name to device
        strlcpy(app->current_device.name, app->text_store, BLE_CONNECT_DEVICE_NAME_LEN);
        app->current_device.has_name = true;

        bool saved = ble_connect_device_save(
            app->storage,
            furi_string_get_cstr(app->file_path),
            &app->current_device,
            app->services,
            app->service_count);

        if(saved) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneSaveSuccess);
        } else {
            scene_manager_previous_scene(app->scene_manager);
        }
        consumed = true;
    }
    return consumed;
}

void ble_connect_scene_save_name_on_exit(void* context) {
    BleConnectApp* app = context;
    text_input_reset(app->text_input);
}
