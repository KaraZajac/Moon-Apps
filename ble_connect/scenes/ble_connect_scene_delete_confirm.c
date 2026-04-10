#include "../ble_connect_app_i.h"

static void ble_connect_scene_delete_confirm_callback(DialogExResult result, void* context) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void ble_connect_scene_delete_confirm_on_enter(void* context) {
    BleConnectApp* app = context;
    DialogEx* dialog = app->dialog_ex;

    dialog_ex_reset(dialog);
    dialog_ex_set_header(dialog, "Delete Device?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(
        dialog,
        app->current_device.has_name ? app->current_device.name : "Saved device",
        64, 24, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(dialog, "Cancel");
    dialog_ex_set_right_button_text(dialog, "Delete");
    dialog_ex_set_result_callback(dialog, ble_connect_scene_delete_confirm_callback);
    dialog_ex_set_context(dialog, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewDialogEx);
}

bool ble_connect_scene_delete_confirm_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            // Delete the file
            storage_simply_remove(app->storage, furi_string_get_cstr(app->file_path));
            // Go back to saved list
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneSavedList);
            consumed = true;
        } else if(event.event == DialogExResultLeft) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_delete_confirm_on_exit(void* context) {
    BleConnectApp* app = context;
    dialog_ex_reset(app->dialog_ex);
}
