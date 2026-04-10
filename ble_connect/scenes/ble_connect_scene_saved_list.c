#include "../ble_connect_app_i.h"

static void ble_connect_scene_saved_list_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_connect_scene_saved_list_on_enter(void* context) {
    BleConnectApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Saved Devices");

    // Ensure directory exists
    storage_simply_mkdir(app->storage, BLE_CONNECT_APP_FOLDER);

    File* dir = storage_file_alloc(app->storage);
    uint32_t idx = 0;

    if(storage_dir_open(dir, BLE_CONNECT_APP_FOLDER)) {
        FileInfo file_info;
        char name[64];
        while(storage_dir_read(dir, &file_info, name, sizeof(name))) {
            // Filter for .ble files
            size_t name_len = strlen(name);
            if(name_len > 4 && strcmp(name + name_len - 4, ".ble") == 0) {
                // Strip extension for display
                char display_name[60];
                strlcpy(display_name, name, sizeof(display_name));
                display_name[name_len - 4] = '\0';

                submenu_add_item(
                    submenu, display_name, idx,
                    ble_connect_scene_saved_list_submenu_callback, app);
                idx++;
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);

    if(idx == 0) {
        submenu_add_item(submenu, "No saved devices", 0, NULL, NULL);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        // Find the file by index
        File* dir = storage_file_alloc(app->storage);
        uint32_t idx = 0;
        bool found = false;

        if(storage_dir_open(dir, BLE_CONNECT_APP_FOLDER)) {
            FileInfo file_info;
            char name[64];
            while(storage_dir_read(dir, &file_info, name, sizeof(name))) {
                size_t name_len = strlen(name);
                if(name_len > 4 && strcmp(name + name_len - 4, ".ble") == 0) {
                    if(idx == event.event) {
                        furi_string_printf(
                            app->file_path, "%s/%s", BLE_CONNECT_APP_FOLDER, name);
                        found = true;
                        break;
                    }
                    idx++;
                }
            }
        }
        storage_dir_close(dir);
        storage_file_free(dir);

        if(found) {
            if(ble_connect_device_load(
                   app->storage, furi_string_get_cstr(app->file_path), &app->current_device)) {
                scene_manager_next_scene(app->scene_manager, BleConnectSceneSavedDevice);
            }
        }
        consumed = true;
    }
    return consumed;
}

void ble_connect_scene_saved_list_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
