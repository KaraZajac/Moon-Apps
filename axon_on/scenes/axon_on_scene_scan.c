#include "../axon_on_app_i.h"

// ── BLE scanning ───────────────────────────────────────────────────────────

// Broadcast helpers — defined in axon_on_app.c
extern bool axon_on_start_broadcast(AxonOnApp* app);
extern void axon_on_stop_broadcast(AxonOnApp* app);

static bool axon_on_start_scan(AxonOnApp* app) {
    GapState state = gap_get_state();

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

    extern void axon_on_scan_callback(GapScanResultData* result, void* context);
    gap_set_scan_callback(axon_on_scan_callback, app);

    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = false,
        .timeout_ms = AXON_SCAN_WINDOW_MS,
    };
    return gap_start_scanning(&params);
}

// ── Scene: Scan ────────────────────────────────────────────────────────────

void axon_on_scene_scan_on_enter(void* context) {
    AxonOnApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->camera_count = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);

    app->scan_start_tick = furi_get_tick();

    with_view_model(
        app->scan_view,
        AxonOnScanModel * model,
        {
            model->camera_count = 0;
            model->cursor = 0;
            model->scroll = 0;
            model->scanning = true;
            model->broadcasting = false;
            model->scan_start_tick = app->scan_start_tick;
        },
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, AxonOnViewScanList);

    if(!axon_on_start_scan(app)) {
        FURI_LOG_E(TAG, "Failed to start scanning");
        app->scanning = false;
        with_view_model(
            app->scan_view,
            AxonOnScanModel * model,
            { model->scanning = false; },
            true);
    }

    furi_timer_start(app->tick_timer, AXON_UI_REFRESH_MS);
}

bool axon_on_scene_scan_on_event(void* context, SceneManagerEvent event) {
    AxonOnApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == AxonOnCustomEventTick) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);

            // Alert on first detection of any camera
            for(uint8_t i = 0; i < app->camera_count; i++) {
                if(!app->cameras[i].alerted) {
                    app->cameras[i].alerted = true;
                    notification_message(app->notifications, &sequence_double_vibro);
                    notification_message(app->notifications, &sequence_blink_red_100);
                    notification_message(app->notifications, &sequence_blink_blue_100);
                }
            }

            // Restart scan window if it expired
            GapState state = gap_get_state();
            if(state != GapStateScanning && app->scanning) {
                FURI_LOG_D(TAG, "Scan window ended, restarting");
                if(!axon_on_start_scan(app)) {
                    FURI_LOG_E(TAG, "Failed to restart scan");
                }
            }

            // Copy state to view model
            with_view_model(
                app->scan_view,
                AxonOnScanModel * model,
                {
                    model->camera_count = app->camera_count;
                    memcpy(model->cameras, app->cameras,
                           app->camera_count * sizeof(AxonDevice));
                    model->scanning = app->scanning;
                    model->broadcasting = app->broadcasting;
                    model->scan_start_tick = app->scan_start_tick;
                    if(model->camera_count == 0) {
                        model->cursor = 0;
                        model->scroll = 0;
                    } else if(model->cursor >= model->camera_count) {
                        model->cursor = model->camera_count - 1;
                    }
                },
                true);

            furi_mutex_release(app->mutex);
            consumed = true;
        }
    }
    return consumed;
}

void axon_on_scene_scan_on_exit(void* context) {
    AxonOnApp* app = context;
    furi_timer_stop(app->tick_timer);

    if(app->broadcasting) {
        axon_on_stop_broadcast(app);
        app->broadcasting = false;
    }

    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
}
