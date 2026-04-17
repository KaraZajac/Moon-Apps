#include "../meshtastic_app_i.h"

typedef enum {
    ConnPhaseWaitConnect,
    ConnPhasePairing,
    ConnPhaseGattDiscovery,
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

    // Configure BLE auth for Meshtastic fixed PIN (legacy pairing)
    gap_set_pairing_method(123456);

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

            // Overall timeout: 20 seconds
            if(app->poll_count > 200) {
                furi_timer_stop(app->timer);
                FURI_LOG_W(TAG, "Timeout in phase %d", connect_phase);
                if(state == GapStateConnected) gap_disconnect(app->connection_handle);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
                return true;
            }

            // Detect disconnect
            if(state != GapStateConnected && connect_phase != ConnPhaseWaitConnect) {
                FURI_LOG_E(TAG, "Disconnected during phase %d", connect_phase);
                furi_timer_stop(app->timer);
                app->state = MeshStateIdle;
                scene_manager_previous_scene(app->scene_manager);
                return true;
            }

            if(connect_phase == ConnPhaseWaitConnect && state == GapStateConnected) {
                // Just connected — wait 500ms, then send pairing request
                app->connection_handle = gap_get_connection_handle();
                meshtastic_register_gatt_callback(app);
                FURI_LOG_I(TAG, "Connected (handle=%d)", app->connection_handle);
                popup_set_text(app->popup, "Pairing...", 64, 36, AlignCenter, AlignCenter);
                connect_phase = ConnPhasePairing;
                app->poll_count = 0;
                // Small delay before pairing
                return true;
            }

            if(connect_phase == ConnPhasePairing) {
                if(app->poll_count == 5) {
                    // 500ms after connect — send pairing request
                    FURI_LOG_I(TAG, "Sending pairing request...");
                    gap_pair(app->connection_handle, false);
                }
                if(app->poll_count == 20) {
                    // 2 seconds after pairing — negotiate MTU
                    FURI_LOG_I(TAG, "Requesting MTU exchange...");
                    ble_gatt_client_exchange_mtu(app->connection_handle);
                }
                if(app->poll_count >= 30) {
                    // 3 seconds after pairing — try GATT
                    FURI_LOG_I(TAG, "Pairing + MTU done, trying GATT...");
                    popup_set_text(app->popup, "Discovering...", 64, 36, AlignCenter, AlignCenter);
                    connect_phase = ConnPhaseGattDiscovery;
                    app->poll_count = 0;
                    app->state = MeshStateReceivingConfig;
                    ble_gatt_client_discover_services(app->connection_handle);
                }
                return true;
            }

            return true;
        } else if(event.event == MeshtasticCustomEventConnected) {
            // GATT chars discovered
            furi_timer_stop(app->timer);
            if(app->char_handles.all_found) {
                FURI_LOG_I(TAG, "Meshtastic chars found, sending want_config");
                // Only send want_config first — don't subscribe or read yet
                // The config_receive scene will handle the rest after this completes
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
            FURI_LOG_E(TAG, "GATT discovery failed");
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
