#include "../lock_tester_app_i.h"

#define SCAN_TIMEOUT_POLLS 100

static bool lock_tester_start_scan(LockTesterApp* app) {
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        uint16_t handle = gap_get_connection_handle();
        gap_disconnect(handle);
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    extern void lock_tester_scan_callback(GapScanResultData* result, void* context);
    gap_set_scan_callback(lock_tester_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = 10000,
    };
    return gap_start_scanning(&params);
}

void lock_tester_scene_scan_on_enter(void* context) {
    LockTesterApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scan_device_count = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);
    app->connect_poll_count = 0;

    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewLoading);

    if(!lock_tester_start_scan(app)) {
        FURI_LOG_E(TAG, "Failed to start scan");
        app->scanning = false;
        scene_manager_next_scene(app->scene_manager, LockTesterSceneScanResults);
        return;
    }

    furi_timer_start(app->timer, 100);
}

bool lock_tester_scene_scan_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == LockTesterCustomEventTick) {
        app->connect_poll_count++;
        GapState state = gap_get_state();

        if(state != GapStateScanning || app->connect_poll_count >= SCAN_TIMEOUT_POLLS) {
            furi_timer_stop(app->timer);
            if(app->scanning) {
                gap_stop_scanning();
                gap_set_scan_callback(NULL, NULL);
                app->scanning = false;
            }
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, LockTesterSceneStart);
            scene_manager_next_scene(app->scene_manager, LockTesterSceneScanResults);
            consumed = true;
        }
    }
    return consumed;
}

void lock_tester_scene_scan_on_exit(void* context) {
    LockTesterApp* app = context;
    furi_timer_stop(app->timer);
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
}
