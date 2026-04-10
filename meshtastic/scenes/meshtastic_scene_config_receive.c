#include "../meshtastic_app_i.h"

void meshtastic_scene_config_receive_on_enter(void* context) {
    MeshtasticApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Receiving Config", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup, "Reading nodes and channels...", 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewPopup);

    app->poll_count = 0;
    // Start polling FromRadio for config data
    furi_timer_start(app->timer, 200);

    // Trigger initial read
    if(app->char_handles.fromradio_handle) {
        ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
    }
}

bool meshtastic_scene_config_receive_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MeshtasticCustomEventFromRadioReady) {
            // Process received data
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->has_read_data && app->read_len > 0) {
                meshtastic_process_from_radio(app, app->read_buf, app->read_len);
                app->has_read_data = false;
            }
            bool complete = app->config_complete;
            furi_mutex_release(app->mutex);

            if(complete) {
                furi_timer_stop(app->timer);
                app->state = MeshStateReady;
                scene_manager_next_scene(app->scene_manager, MeshtasticSceneMainMenu);
                return true;
            }

            // Read more config data
            ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
            return true;
        } else if(event.event == MeshtasticCustomEventTimerTick) {
            app->poll_count++;
            // Also poll read in case notifications aren't working
            if(app->char_handles.fromradio_handle && !app->has_read_data) {
                ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
            }
            // Timeout after 30 seconds
            if(app->poll_count > 150) {
                furi_timer_stop(app->timer);
                FURI_LOG_W(TAG, "Config timeout, proceeding with partial data");
                app->state = MeshStateReady;
                scene_manager_next_scene(app->scene_manager, MeshtasticSceneMainMenu);
                return true;
            }
            return true;
        } else if(event.event == MeshtasticCustomEventGattError) {
            furi_timer_stop(app->timer);
            gap_disconnect(app->connection_handle);
            app->state = MeshStateIdle;
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MeshtasticSceneStart);
            return true;
        }
    }
    return false;
}

void meshtastic_scene_config_receive_on_exit(void* context) {
    MeshtasticApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
