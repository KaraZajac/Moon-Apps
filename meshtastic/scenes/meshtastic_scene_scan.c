#include "../meshtastic_app_i.h"

#define SCAN_POLL_LIMIT 100 // 10 seconds

static void meshtastic_scene_scan_select_callback(void* context, uint32_t index) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void meshtastic_scene_scan_on_enter(void* context) {
    MeshtasticApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scan_device_count = 0;
    app->state = MeshStateScanning;
    app->poll_count = 0;
    furi_mutex_release(app->mutex);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewLoading);

    // Disconnect phone BLE if connected
    GapState gap_state = gap_get_state();
    if(gap_state == GapStateConnected) {
        uint16_t handle = gap_get_connection_handle();
        gap_disconnect(handle);
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    extern void meshtastic_scan_callback(GapScanResultData* result, void* context);
    gap_set_scan_callback(meshtastic_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = 10000,
    };
    if(!gap_start_scanning(&params)) {
        FURI_LOG_E(TAG, "Failed to start scanning");
        app->state = MeshStateIdle;
        // Show empty results
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, MeshtasticSceneStart);
        return;
    }

    furi_timer_start(app->timer, 100);
}

bool meshtastic_scene_scan_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == MeshtasticCustomEventTimerTick) {
        app->poll_count++;
        GapState state = gap_get_state();

        if(state != GapStateScanning || app->poll_count >= SCAN_POLL_LIMIT) {
            furi_timer_stop(app->timer);
            if(app->state == MeshStateScanning) {
                gap_stop_scanning();
                gap_set_scan_callback(NULL, NULL);
                app->state = MeshStateIdle;
            }

            // Build device list submenu
            Submenu* submenu = app->submenu;
            submenu_reset(submenu);
            submenu_set_header(submenu, "Mesh Nodes");

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->scan_device_count == 0) {
                submenu_add_item(submenu, "[No nodes found]", 0xFF, NULL, NULL);
            } else {
                for(uint8_t i = 0; i < app->scan_device_count; i++) {
                    MeshScanDevice* dev = &app->scan_devices[i];
                    char label[48];
                    if(dev->has_name && dev->name[0]) {
                        snprintf(label, sizeof(label), "%s (%ddBm)", dev->name, dev->rssi);
                    } else {
                        snprintf(
                            label,
                            sizeof(label),
                            "%02X:%02X:%02X:%02X (%ddBm)",
                            dev->address[5], dev->address[4],
                            dev->address[3], dev->address[2],
                            dev->rssi);
                    }
                    submenu_add_item(submenu, label, i, meshtastic_scene_scan_select_callback, app);
                }
            }
            submenu_add_item(submenu, "[Rescan]", 0xFE, meshtastic_scene_scan_select_callback, app);
            furi_mutex_release(app->mutex);

            view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewSubmenu);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == 0xFE) {
            // Rescan
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MeshtasticSceneStart);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneScan);
            return true;
        } else if(event.event < app->scan_device_count) {
            // Selected a device
            app->selected_device_idx = event.event;
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneConnecting);
            return true;
        }
    }

    return false;
}

void meshtastic_scene_scan_on_exit(void* context) {
    MeshtasticApp* app = context;
    furi_timer_stop(app->timer);
    if(app->state == MeshStateScanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->state = MeshStateIdle;
    }
    submenu_reset(app->submenu);
}
