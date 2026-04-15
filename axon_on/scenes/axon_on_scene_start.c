#include "../axon_on_app_i.h"

enum {
    AxonOnStartSubmenuScan,
    AxonOnStartSubmenuAbout,
};

static void axon_on_scene_start_submenu_callback(void* context, uint32_t index) {
    AxonOnApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void axon_on_scene_start_on_enter(void* context) {
    AxonOnApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Axon On");
    submenu_add_item(
        submenu,
        "Scan for Cameras",
        AxonOnStartSubmenuScan,
        axon_on_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "About",
        AxonOnStartSubmenuAbout,
        axon_on_scene_start_submenu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, AxonOnViewSubmenu);
}

bool axon_on_scene_start_on_event(void* context, SceneManagerEvent event) {
    AxonOnApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == AxonOnStartSubmenuScan) {
            scene_manager_next_scene(app->scene_manager, AxonOnSceneScan);
            consumed = true;
        } else if(event.event == AxonOnStartSubmenuAbout) {
            scene_manager_next_scene(app->scene_manager, AxonOnSceneAbout);
            consumed = true;
        }
    }
    return consumed;
}

void axon_on_scene_start_on_exit(void* context) {
    AxonOnApp* app = context;
    submenu_reset(app->submenu);
}
