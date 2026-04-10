#include "../meshtastic_app_i.h"

enum {
    MeshtasticStartScan,
};

static void meshtastic_scene_start_callback(void* context, uint32_t index) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void meshtastic_scene_start_on_enter(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Meshtastic");
    submenu_add_item(
        app->submenu, "Scan for Nodes", MeshtasticStartScan,
        meshtastic_scene_start_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewSubmenu);
}

bool meshtastic_scene_start_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == MeshtasticStartScan) {
        scene_manager_next_scene(app->scene_manager, MeshtasticSceneScan);
        return true;
    }
    return false;
}

void meshtastic_scene_start_on_exit(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);
}
