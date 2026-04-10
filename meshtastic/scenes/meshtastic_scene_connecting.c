#include "../meshtastic_app_i.h"

void meshtastic_scene_connecting_on_enter(void* context) {
    MeshtasticApp* app = context;
    MeshScanDevice* dev = &app->scan_devices[app->selected_device_idx];

    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);
    snprintf(
        app->text_store, sizeof(app->text_store), "%s",
        dev->has_name ? dev->name : "Meshtastic Node");
    popup_set_text(app->popup, app->text_store, 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewPopup);

    app->state = MeshStateConnecting;
    app->poll_count = 0;
    app->config_complete = false;
    app->node_count = 0;
    app->message_count = 0;
    app->channel_count = 0;

    // Disconnect existing BLE if needed
    GapState gap_state = gap_get_state();
    if(gap_state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    if(!gap_connect(dev->address_type, dev->address)) {
        FURI_LOG_E(TAG, "gap_connect failed");
        app->state = MeshStateIdle;
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    furi_timer_start(app->timer, 100);
}

bool meshtastic_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MeshtasticCustomEventTimerTick) {
            app->poll_count++;
            GapState state = gap_get_state();

            if(state == GapStateConnected && app->state == MeshStateConnecting) {
                furi_timer_stop(app->timer);
                app->connection_handle = gap_get_connection_handle();
                FURI_LOG_I(TAG, "Connected, discovering services...");
                app->state = MeshStateReceivingConfig;

                // Discover services to find Meshtastic GATT
                ble_gatt_client_discover_services(app->connection_handle);
                return true;
            } else if(app->poll_count > 100) {
                // Timeout
                furi_timer_stop(app->timer);
                gap_disconnect(0);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
                return true;
            }
        } else if(event.event == MeshtasticCustomEventConnected) {
            // GATT chars discovered — start config
            furi_timer_stop(app->timer);
            if(app->char_handles.all_found) {
                FURI_LOG_I(TAG, "Meshtastic chars found, requesting config");
                // Subscribe to FromNum notifications
                ble_gatt_client_subscribe_notifications(
                    app->connection_handle, app->char_handles.fromnum_handle, true);
                // Request config
                meshtastic_send_want_config(app);
                scene_manager_next_scene(app->scene_manager, MeshtasticSceneConfigReceive);
            } else {
                FURI_LOG_E(TAG, "Meshtastic characteristics not found");
                gap_disconnect(app->connection_handle);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
            }
            return true;
        } else if(event.event == MeshtasticCustomEventGattError) {
            furi_timer_stop(app->timer);
            gap_disconnect(app->connection_handle);
            app->state = MeshStateIdle;
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
    }
    return false;
}

void meshtastic_scene_connecting_on_exit(void* context) {
    MeshtasticApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
