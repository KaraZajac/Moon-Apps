#include "../race_auditor_app_i.h"

static void race_scan_results_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((RaceAuditorApp*)ctx)->view_dispatcher, idx);
}

void race_scene_scan_results_on_enter(void* context) {
    RaceAuditorApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Device");

    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        RaceDevice* dev = &app->scan_devices[i];
        // Build label: "Name (-XXdBm)" or "XX:XX:XX:XX:XX:XX (-XXdBm)"
        static char labels[RACE_MAX_SCAN_DEVICES][48];
        if(dev->has_name) {
            snprintf(labels[i], sizeof(labels[i]), "%s (%ddBm)", dev->name, dev->rssi);
        } else {
            snprintf(labels[i], sizeof(labels[i]),
                "%02X:%02X:%02X:%02X:%02X:%02X (%ddBm)",
                dev->address[5], dev->address[4], dev->address[3],
                dev->address[2], dev->address[1], dev->address[0],
                dev->rssi);
        }
        submenu_add_item(app->submenu, labels[i], i, race_scan_results_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RaceViewSubmenu);
}

bool race_scene_scan_results_on_event(void* context, SceneManagerEvent event) {
    RaceAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event < app->scan_device_count) {
        app->selected_device_idx = event.event;
        app->current_device = app->scan_devices[event.event];
        scene_manager_next_scene(app->scene_manager, RaceSceneConnecting);
        return true;
    }
    return false;
}

void race_scene_scan_results_on_exit(void* context) {
    RaceAuditorApp* app = context;
    submenu_reset(app->submenu);
}
