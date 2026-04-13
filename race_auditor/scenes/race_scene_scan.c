#include "../race_auditor_app_i.h"

#define SCAN_TIMEOUT_MS 10000

void race_scene_scan_on_enter(void* context) {
    RaceAuditorApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scan_device_count = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);

    view_dispatcher_switch_to_view(app->view_dispatcher, RaceViewLoading);

    gap_set_scan_callback(race_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = SCAN_TIMEOUT_MS,
    };
    if(!gap_start_scanning(&params)) {
        FURI_LOG_E(TAG, "Failed to start scan");
        app->scanning = false;
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    furi_timer_start(app->timer, 200);
}

bool race_scene_scan_on_event(void* context, SceneManagerEvent event) {
    RaceAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == RaceCustomEventTick) {
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
            furi_timer_stop(app->timer);
            gap_set_scan_callback(NULL, NULL);

            if(app->scan_device_count > 0) {
                scene_manager_next_scene(app->scene_manager, RaceSceneScanResults);
            } else {
                scene_manager_previous_scene(app->scene_manager);
            }
        }
        return true;
    }
    return false;
}

void race_scene_scan_on_exit(void* context) {
    RaceAuditorApp* app = context;
    furi_timer_stop(app->timer);
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
}
