#include "../ble_connect_app_i.h"

void ble_connect_scene_save_success_on_enter(void* context) {
    BleConnectApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Saved!", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->current_device.name, 64, 36, AlignCenter, AlignCenter);
    popup_set_timeout(app->popup, 1500);
    popup_enable_timeout(app->popup);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, NULL);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewPopup);
}

bool ble_connect_scene_save_success_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        // After save, go back to services or start
        if(app->connected) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneServices);
        } else {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneStart);
        }
        consumed = true;
    }
    return consumed;
}

void ble_connect_scene_save_success_on_exit(void* context) {
    BleConnectApp* app = context;
    popup_reset(app->popup);
}
