#include "../ble_connect_app_i.h"

#define SCAN_RESULTS_RESCAN_IDX 0xFE

static void ble_connect_scene_scan_results_submenu_callback(void* context, uint32_t index) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_connect_scene_scan_results_on_enter(void* context) {
    BleConnectApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Scan Results");

    furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
    if(app->scan_device_count == 0) {
        submenu_add_item(
            submenu, "[No devices found]", SCAN_RESULTS_RESCAN_IDX, NULL, NULL);
    } else {
        for(uint8_t i = 0; i < app->scan_device_count; i++) {
            BleConnectDevice* dev = &app->scan_devices[i];
            char label[48];
            if(dev->has_name && dev->name[0]) {
                snprintf(label, sizeof(label), "%s (%ddBm)", dev->name, dev->rssi);
            } else {
                char mac[18];
                ble_connect_device_mac_to_str(dev->address, mac, sizeof(mac));
                snprintf(label, sizeof(label), "%s (%ddBm)", mac, dev->rssi);
            }
            submenu_add_item(
                submenu, label, i,
                ble_connect_scene_scan_results_submenu_callback, app);
        }
    }
    furi_mutex_release(app->scan_mutex);

    // Always show rescan option at the bottom
    submenu_add_item(
        submenu, "[Rescan]", SCAN_RESULTS_RESCAN_IDX,
        ble_connect_scene_scan_results_submenu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewSubmenu);
}

bool ble_connect_scene_scan_results_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SCAN_RESULTS_RESCAN_IDX) {
            // Go back to start then into scan
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneStart);
            scene_manager_next_scene(app->scene_manager, BleConnectSceneScan);
            consumed = true;
        } else if(event.event < app->scan_device_count) {
            app->selected_device_idx = event.event;
            furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
            memcpy(&app->current_device, &app->scan_devices[event.event], sizeof(BleConnectDevice));
            furi_mutex_release(app->scan_mutex);
            scene_manager_next_scene(app->scene_manager, BleConnectSceneDeviceInfo);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_scan_results_on_exit(void* context) {
    BleConnectApp* app = context;
    submenu_reset(app->submenu);
}
