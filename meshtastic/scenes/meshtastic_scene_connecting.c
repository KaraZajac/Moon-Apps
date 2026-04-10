#include "../meshtastic_app_i.h"

typedef enum {
    ConnPhaseWaitConnect,
    ConnPhaseWaitPairing,
    ConnPhaseWaitGatt,
} ConnectPhase;

static ConnectPhase connect_phase;

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
    connect_phase = ConnPhaseWaitConnect;

    // Disconnect existing BLE if needed
    GapState gap_state = gap_get_state();
    if(gap_state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    // Set fixed PIN for Meshtastic pairing (default: 123456)
    gap_set_fixed_pin(123456);

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

            // Timeout after 15 seconds
            if(app->poll_count > 150) {
                furi_timer_stop(app->timer);
                FURI_LOG_W(TAG, "Connection/pairing timeout");
                gap_disconnect(app->connection_handle);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
                return true;
            }

            GapState state = gap_get_state();

            if(connect_phase == ConnPhaseWaitConnect && state == GapStateConnected) {
                // Step 1: Connected — initiate pairing
                app->connection_handle = gap_get_connection_handle();
                FURI_LOG_I(TAG, "Connected (handle=%d), initiating pairing...", app->connection_handle);
                popup_set_text(app->popup, "Pairing...", 64, 36, AlignCenter, AlignCenter);
                connect_phase = ConnPhaseWaitPairing;
                app->poll_count = 0; // Reset timeout for pairing phase

                if(!gap_pair(app->connection_handle, true)) {
                    FURI_LOG_W(TAG, "Pairing request failed, trying GATT discovery anyway");
                    // Some devices don't need explicit pairing — try GATT directly
                    connect_phase = ConnPhaseWaitGatt;
                    app->state = MeshStateReceivingConfig;
                    ble_gatt_client_discover_services(app->connection_handle);
                }
                return true;
            }

            if(connect_phase == ConnPhaseWaitPairing && state == GapStateConnected) {
                // Step 2: Wait for pairing to complete, then try GATT
                // Pairing complete is signaled by ACI_GAP_PAIRING_COMPLETE event
                // which the GAP layer handles. After ~2 seconds, try GATT discovery.
                if(app->poll_count > 20) {
                    FURI_LOG_I(TAG, "Pairing wait done, discovering services...");
                    popup_set_text(app->popup, "Discovering...", 64, 36, AlignCenter, AlignCenter);
                    connect_phase = ConnPhaseWaitGatt;
                    app->state = MeshStateReceivingConfig;
                    app->poll_count = 0;
                    ble_gatt_client_discover_services(app->connection_handle);
                }
                return true;
            }

            if(connect_phase == ConnPhaseWaitPairing && state != GapStateConnected) {
                // Pairing failed — disconnected
                FURI_LOG_E(TAG, "Disconnected during pairing");
                furi_timer_stop(app->timer);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
                return true;
            }

            return true;
        } else if(event.event == MeshtasticCustomEventConnected) {
            // GATT characteristics discovered — start config
            furi_timer_stop(app->timer);
            if(app->char_handles.all_found) {
                FURI_LOG_I(TAG, "Meshtastic chars found, requesting config");
                ble_gatt_client_subscribe_notifications(
                    app->connection_handle, app->char_handles.fromnum_handle, true);
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
            if(connect_phase == ConnPhaseWaitGatt) {
                // GATT failed after pairing — might need longer wait
                FURI_LOG_E(TAG, "GATT error after pairing");
                furi_timer_stop(app->timer);
                gap_disconnect(app->connection_handle);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
            }
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
