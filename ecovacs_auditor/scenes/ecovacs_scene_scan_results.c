#include "../ecovacs_app_i.h"

static void eco_results_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((EcovacsAuditorApp*)ctx)->view_dispatcher, idx);
}

void ecovacs_scene_scan_results_on_enter(void* context) {
    EcovacsAuditorApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Device");

    // Show Ecovacs devices first, then others
    static char labels[ECOVACS_MAX_SCAN_DEVICES][48];

    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        EcovacsDevice* dev = &app->scan_devices[i];
        if(dev->has_name) {
            snprintf(labels[i], sizeof(labels[i]), "%s%s (%ddBm)",
                dev->is_ecovacs ? "*" : "",
                dev->name, dev->rssi);
        } else {
            snprintf(labels[i], sizeof(labels[i]),
                "%02X:%02X:%02X:%02X:%02X:%02X (%ddBm)",
                dev->address[5], dev->address[4], dev->address[3],
                dev->address[2], dev->address[1], dev->address[0],
                dev->rssi);
        }
        submenu_add_item(app->submenu, labels[i], i, eco_results_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, EcovacsViewSubmenu);
}

bool ecovacs_scene_scan_results_on_event(void* context, SceneManagerEvent event) {
    EcovacsAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event < app->scan_device_count) {
        app->selected_device_idx = event.event;
        app->current_device = app->scan_devices[event.event];
        scene_manager_next_scene(app->scene_manager, EcovacsSceneConnecting);
        return true;
    }
    return false;
}

void ecovacs_scene_scan_results_on_exit(void* context) {
    EcovacsAuditorApp* app = context;
    submenu_reset(app->submenu);
}
