#include "../tracker_detector_app_i.h"

#define SCAN_WINDOW_MS     10000  // 10s scan windows
#define SCAN_RESTART_POLLS 2      // Wait 2 ticks (1s) between scan windows
#define UI_REFRESH_MS      500

static bool tracker_detector_start_scan(TrackerDetectorApp* app) {
    GapState state = gap_get_state();

    // Disconnect companion app if connected
    if(state == GapStateConnected) {
        uint16_t handle = gap_get_connection_handle();
        gap_disconnect(handle);
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
        if(gap_get_state() == GapStateConnected) {
            FURI_LOG_E(TAG, "Failed to disconnect for scan");
            return false;
        }
    }

    extern void tracker_detector_scan_callback(GapScanResultData* result, void* context);
    gap_set_scan_callback(tracker_detector_scan_callback, app);

    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = false,  // Passive scan — we only need AD data
        .timeout_ms = SCAN_WINDOW_MS,
    };
    return gap_start_scanning(&params);
}

void tracker_detector_scene_scan_on_enter(void* context) {
    TrackerDetectorApp* app = context;

    // Reset scan state
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->tracker_count = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);

    app->scan_start_tick = furi_get_tick();

    // Reset view model
    with_view_model(
        app->scan_view,
        TrackerDetectorScanModel * model,
        {
            model->tracker_count = 0;
            model->cursor = 0;
            model->scroll = 0;
            model->scanning = true;
            model->scan_start_tick = app->scan_start_tick;
        },
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, TrackerDetectorViewScanList);

    if(!tracker_detector_start_scan(app)) {
        FURI_LOG_E(TAG, "Failed to start scanning");
        app->scanning = false;
        with_view_model(
            app->scan_view,
            TrackerDetectorScanModel * model,
            { model->scanning = false; },
            true);
    }

    // Start UI refresh timer
    furi_timer_start(app->tick_timer, UI_REFRESH_MS);
}

bool tracker_detector_scene_scan_on_event(void* context, SceneManagerEvent event) {
    TrackerDetectorApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == TrackerDetectorCustomEventTick) {
            // Check for "following" alerts and notify
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(uint8_t i = 0; i < app->tracker_count; i++) {
                if(app->trackers[i].following && !app->trackers[i].alerted) {
                    app->trackers[i].alerted = true;
                    notification_message(app->notifications, &sequence_double_vibro);
                    notification_message(app->notifications, &sequence_blink_red_100);
                }
            }

            // If scan window finished, restart it
            GapState state = gap_get_state();
            if(state != GapStateScanning && app->scanning) {
                FURI_LOG_D(TAG, "Scan window ended, restarting");
                if(!tracker_detector_start_scan(app)) {
                    FURI_LOG_E(TAG, "Failed to restart scan");
                }
            }

            // Copy tracker state to view model for rendering
            with_view_model(
                app->scan_view,
                TrackerDetectorScanModel * model,
                {
                    model->tracker_count = app->tracker_count;
                    memcpy(model->trackers, app->trackers,
                           app->tracker_count * sizeof(TrackerDevice));
                    model->scanning = app->scanning;
                    model->scan_start_tick = app->scan_start_tick;
                    // Clamp cursor/scroll
                    if(model->tracker_count == 0) {
                        model->cursor = 0;
                        model->scroll = 0;
                    } else {
                        if(model->cursor >= model->tracker_count) {
                            model->cursor = model->tracker_count - 1;
                        }
                    }
                },
                true);

            furi_mutex_release(app->mutex);
            consumed = true;
        }
    }
    return consumed;
}

void tracker_detector_scene_scan_on_exit(void* context) {
    TrackerDetectorApp* app = context;
    furi_timer_stop(app->tick_timer);

    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
}
