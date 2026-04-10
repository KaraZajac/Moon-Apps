#include "../whisper_pair_app_i.h"

#define SCAN_WINDOW_MS  10000
#define UI_REFRESH_MS   500

static bool wp_start_scan(WhisperPairApp* app) {
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    extern void wp_scan_callback(GapScanResultData* result, void* context);
    gap_set_scan_callback(wp_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = SCAN_WINDOW_MS,
    };
    return gap_start_scanning(&params);
}

void whisper_pair_scene_scan_on_enter(void* context) {
    WhisperPairApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->device_count = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);

    with_view_model(app->scan_view, WpScanModel * m, {
        m->device_count = 0;
        m->cursor = 0;
        m->scroll = 0;
        m->scanning = true;
    }, true);

    view_dispatcher_switch_to_view(app->view_dispatcher, WpViewScanList);

    if(!wp_start_scan(app)) {
        app->scanning = false;
        with_view_model(app->scan_view, WpScanModel * m, { m->scanning = false; }, true);
    }

    furi_timer_start(app->timer, UI_REFRESH_MS);
}

bool whisper_pair_scene_scan_on_event(void* context, SceneManagerEvent event) {
    WhisperPairApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == WpCustomEventTick) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);

        // Restart scan if window ended
        GapState state = gap_get_state();
        if(state != GapStateScanning && app->scanning) {
            if(!wp_start_scan(app)) {
                app->scanning = false;
            }
        }

        // Sync to view model
        with_view_model(app->scan_view, WpScanModel * m, {
            m->device_count = app->device_count;
            memcpy(m->devices, app->devices, app->device_count * sizeof(WpDevice));
            m->scanning = app->scanning;
            if(m->device_count == 0) { m->cursor = 0; m->scroll = 0; }
            else if(m->cursor >= m->device_count) m->cursor = m->device_count - 1;
        }, true);

        furi_mutex_release(app->mutex);
        return true;
    }
    return false;
}

void whisper_pair_scene_scan_on_exit(void* context) {
    WhisperPairApp* app = context;
    furi_timer_stop(app->timer);
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
}
