#include "../coros_auditor_app_i.h"

static void coros_results_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((CorosAuditorApp*)ctx)->view_dispatcher, idx);
}

void coros_scene_scan_results_on_enter(void* context) {
    CorosAuditorApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Watch");
    static char labels[COROS_MAX_SCAN_DEVICES][48];
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        CorosDevice* dev = &app->scan_devices[i];
        if(dev->has_name)
            snprintf(labels[i], sizeof(labels[i]), "%s%s (%ddBm)", dev->is_coros ? "*" : "", dev->name, dev->rssi);
        else
            snprintf(labels[i], sizeof(labels[i]), "%02X:%02X:%02X:%02X:%02X:%02X (%ddBm)",
                dev->address[5], dev->address[4], dev->address[3],
                dev->address[2], dev->address[1], dev->address[0], dev->rssi);
        submenu_add_item(app->submenu, labels[i], i, coros_results_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, CorosViewSubmenu);
}

bool coros_scene_scan_results_on_event(void* context, SceneManagerEvent event) {
    CorosAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < app->scan_device_count) {
        app->current_device = app->scan_devices[event.event];
        scene_manager_next_scene(app->scene_manager, CorosSceneConnecting);
        return true;
    }
    return false;
}

void coros_scene_scan_results_on_exit(void* context) {
    CorosAuditorApp* app = context;
    submenu_reset(app->submenu);
}
