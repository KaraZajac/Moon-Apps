#include "../ble_connect_app_i.h"

void ble_connect_scene_connecting_on_enter(void* context) {
    BleConnectApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);
    char mac[18];
    ble_connect_device_mac_to_str(app->current_device.address, mac, sizeof(mac));
    snprintf(
        app->text_store,
        sizeof(app->text_store),
        "%s\n%s",
        app->current_device.has_name ? app->current_device.name : "Device",
        mac);
    popup_set_text(app->popup, app->text_store, 64, 36, AlignCenter, AlignCenter);

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewPopup);

    app->connected = false;
    app->connect_poll_count = 0;

    // If BLE is connected to phone, disconnect first
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        FURI_LOG_I(TAG, "Disconnecting existing BLE connection before connect");
        uint16_t handle = gap_get_connection_handle();
        gap_disconnect(handle);
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    // Initiate connection
    if(!gap_connect(app->current_device.address_type, app->current_device.address)) {
        FURI_LOG_E(TAG, "gap_connect failed, state=%d", gap_get_state());
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    // Poll connection state every 100ms (timer is periodic, shared with scan)
    furi_timer_start(app->connect_timer, 100);
}

bool ble_connect_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectCustomEventConnectTimeout) {
            // Timer tick — poll connection state
            app->connect_poll_count++;

            GapState state = gap_get_state();
            if(state == GapStateConnected) {
                furi_timer_stop(app->connect_timer);
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                FURI_LOG_I(TAG, "Connected, handle=%d", app->connection_handle);

                ble_connect_register_gatt_callback(app);
                // Discover GATT services
                ble_gatt_client_discover_services(app->connection_handle);
                scene_manager_next_scene(app->scene_manager, BleConnectSceneServices);
                consumed = true;
            } else if(app->connect_poll_count > 100) {
                // 10 second timeout
                furi_timer_stop(app->connect_timer);
                FURI_LOG_W(TAG, "Connection timeout");
                if(app->connection_handle) {
                    gap_disconnect(app->connection_handle);
                }
                app->connected = false;
                scene_manager_previous_scene(app->scene_manager);
                consumed = true;
            }
        } else if(event.event == BleConnectCustomEventPinCodeShow) {
            furi_timer_stop(app->connect_timer);
            scene_manager_next_scene(app->scene_manager, BleConnectScenePairingPin);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_connecting_on_exit(void* context) {
    BleConnectApp* app = context;
    furi_timer_stop(app->connect_timer);
    popup_reset(app->popup);
}
