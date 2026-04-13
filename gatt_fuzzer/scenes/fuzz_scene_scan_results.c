#include "../gatt_fuzzer_app_i.h"

static void fuzz_results_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((GattFuzzerApp*)ctx)->view_dispatcher, idx);
}

void fuzz_scene_scan_results_on_enter(void* context) {
    GattFuzzerApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Target");
    static char labels[FUZZ_MAX_SCAN_DEVICES][48];
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        FuzzDevice* dev = &app->scan_devices[i];
        if(dev->has_name)
            snprintf(labels[i], sizeof(labels[i]), "%s (%ddBm)", dev->name, dev->rssi);
        else
            snprintf(labels[i], sizeof(labels[i]), "%02X:%02X:%02X:%02X:%02X:%02X (%ddBm)",
                dev->address[5], dev->address[4], dev->address[3],
                dev->address[2], dev->address[1], dev->address[0], dev->rssi);
        submenu_add_item(app->submenu, labels[i], i, fuzz_results_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, FuzzViewSubmenu);
}

bool fuzz_scene_scan_results_on_event(void* context, SceneManagerEvent event) {
    GattFuzzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < app->scan_device_count) {
        app->current_device = app->scan_devices[event.event];
        scene_manager_next_scene(app->scene_manager, FuzzSceneConnecting);
        return true;
    }
    return false;
}

void fuzz_scene_scan_results_on_exit(void* context) {
    GattFuzzerApp* app = context;
    submenu_reset(app->submenu);
}
