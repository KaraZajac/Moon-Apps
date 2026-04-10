#include "../meshtastic_app_i.h"

static bool waiting_for_write;

void meshtastic_scene_config_receive_on_enter(void* context) {
    MeshtasticApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Receiving Config", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup, "Reading nodes and channels...", 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewPopup);

    app->poll_count = 0;
    waiting_for_write = true; // Wait for want_config write to complete

    // Start timeout timer
    furi_timer_start(app->timer, 200);
}

bool meshtastic_scene_config_receive_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MeshtasticCustomEventWriteComplete) {
            // want_config write done — now start reading responses
            waiting_for_write = false;
            FURI_LOG_I(TAG, "Config request sent, waiting for device...");
            furi_delay_ms(500); // Let device prepare responses
            FURI_LOG_I(TAG, "Reading config responses...");
            ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
            return true;
        } else if(event.event == MeshtasticCustomEventFromRadioReady) {
            // Copy data out under mutex, then process without holding it
            // (process_from_radio acquires mutex internally)
            uint8_t* buf = NULL;
            uint16_t buf_len = 0;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->has_read_data && app->read_len > 0) {
                buf_len = app->read_len;
                buf = malloc(buf_len);
                if(buf) memcpy(buf, app->read_buf, buf_len);
                app->has_read_data = false;
            }
            furi_mutex_release(app->mutex);

            if(buf) {
                meshtastic_process_from_radio(app, buf, buf_len);
                free(buf);
            }

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool complete = app->config_complete;
            furi_mutex_release(app->mutex);

            if(complete) {
                furi_timer_stop(app->timer);
                app->state = MeshStateReady;
                FURI_LOG_I(
                    TAG, "Config complete! %d nodes, %d channels",
                    app->node_count, app->channel_count);
                scene_manager_next_scene(app->scene_manager, MeshtasticSceneMainMenu);
                return true;
            }

            // Read next piece — small delay so GATT op completes
            furi_delay_ms(50);
            ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
            return true;
        } else if(event.event == MeshtasticCustomEventTimerTick) {
            app->poll_count++;

            if(waiting_for_write) return true; // Still waiting for write

            // Timeout after 30 seconds
            if(app->poll_count > 150) {
                furi_timer_stop(app->timer);
                FURI_LOG_W(TAG, "Config timeout (%d nodes, %d channels)",
                    app->node_count, app->channel_count);
                app->state = MeshStateReady;
                scene_manager_next_scene(app->scene_manager, MeshtasticSceneMainMenu);
                return true;
            }
            return true;
        } else if(event.event == MeshtasticCustomEventGattError) {
            furi_timer_stop(app->timer);
            FURI_LOG_E(TAG, "GATT error during config");
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
