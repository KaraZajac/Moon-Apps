#include "../ble_connect_app_i.h"

#define SCAN_TIMEOUT_POLLS 100  // 100 * 100ms = 10 seconds

static bool ble_connect_start_scan(BleConnectApp* app) {
    // If BLE is connected (e.g. phone companion), disconnect first
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        FURI_LOG_I(TAG, "Disconnecting existing BLE connection for scan");
        uint16_t handle = gap_get_connection_handle();
        gap_disconnect(handle);
        // Wait briefly for disconnect to complete
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            state = gap_get_state();
            if(state != GapStateConnected) break;
        }
        if(state == GapStateConnected) {
            FURI_LOG_E(TAG, "Failed to disconnect existing connection");
            return false;
        }
    }

    // Stop advertising if active
    state = gap_get_state();
    if(state == GapStateAdvFast || state == GapStateAdvLowPower) {
        // gap_start_scanning handles this internally, but log it
        FURI_LOG_I(TAG, "Will pause advertising for scan");
    }

    extern void ble_connect_scan_callback_fn(GapScanResultData* result, void* context);
    gap_set_scan_callback(ble_connect_scan_callback_fn, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = 10000,
    };
    return gap_start_scanning(&params);
}

void ble_connect_scene_scan_on_enter(void* context) {
    BleConnectApp* app = context;

    // Reset scan state
    furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
    app->scan_device_count = 0;
    app->scanning = true;
    furi_mutex_release(app->scan_mutex);
    app->connect_poll_count = 0;

    // Show loading view
    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewLoading);

    if(!ble_connect_start_scan(app)) {
        FURI_LOG_E(TAG, "Failed to start scanning");
        app->scanning = false;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, BleConnectSceneStart);
        scene_manager_next_scene(app->scene_manager, BleConnectSceneScanResults);
        return;
    }

    // Use the periodic timer to poll for scan completion
    furi_timer_start(app->connect_timer, 100);
}

bool ble_connect_scene_scan_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectCustomEventConnectTimeout) {
            // Timer fired — check if scan is done or timed out
            app->connect_poll_count++;
            GapState state = gap_get_state();

            if(state != GapStateScanning || app->connect_poll_count >= SCAN_TIMEOUT_POLLS) {
                furi_timer_stop(app->connect_timer);
                if(app->scanning) {
                    gap_stop_scanning();
                    gap_set_scan_callback(NULL, NULL);
                    app->scanning = false;
                }
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, BleConnectSceneStart);
                scene_manager_next_scene(app->scene_manager, BleConnectSceneScanResults);
                consumed = true;
            }
        }
    }
    return consumed;
}

void ble_connect_scene_scan_on_exit(void* context) {
    BleConnectApp* app = context;
    furi_timer_stop(app->connect_timer);
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
}
