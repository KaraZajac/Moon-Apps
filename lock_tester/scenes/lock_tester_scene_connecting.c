#include "../lock_tester_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100

void lock_tester_scene_connecting_on_enter(void* context) {
    LockTesterApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
        app->current_device.address[5], app->current_device.address[4],
        app->current_device.address[3], app->current_device.address[2],
        app->current_device.address[1], app->current_device.address[0]);

    // Use the popup text area for device info (static buffer not needed since popup copies)
    popup_set_text(app->popup,
        app->current_device.has_name ? app->current_device.name : mac,
        64, 36, AlignCenter, AlignCenter);

    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewPopup);

    app->connected = false;
    app->connect_poll_count = 0;
    app->service_count = 0;
    app->char_count = 0;

    // Disconnect existing
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    if(!gap_connect(app->current_device.address_type, app->current_device.address)) {
        FURI_LOG_E(TAG, "gap_connect failed");
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    furi_timer_start(app->timer, 100);
}

bool lock_tester_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == LockTesterCustomEventTick) {
            app->connect_poll_count++;
            GapState state = gap_get_state();

            if(state == GapStateConnected) {
                furi_timer_stop(app->timer);
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                FURI_LOG_I(TAG, "Connected, handle=%d", app->connection_handle);

                // Discover GATT services
                ble_gatt_client_discover_services(app->connection_handle);
                popup_set_header(app->popup, "Discovering...", 64, 20, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "GATT services", 64, 36, AlignCenter, AlignCenter);
                consumed = true;
            } else if(app->connect_poll_count >= CONNECT_TIMEOUT_POLLS) {
                furi_timer_stop(app->timer);
                FURI_LOG_W(TAG, "Connection timeout");
                scene_manager_previous_scene(app->scene_manager);
                consumed = true;
            }
        } else if(event.event == LockTesterCustomEventServicesDiscovered) {
            // Try to auto-identify lock profile from GATT services
            if(!app->current_device.profile) {
                for(uint8_t i = 0; i < app->service_count; i++) {
                    if(app->services[i].uuid_type == 1) {
                        const LockProfile* p =
                            lock_profile_match_by_service(app->services[i].uuid_16);
                        if(p) {
                            app->current_device.profile = p;
                            break;
                        }
                    }
                }
            }
            scene_manager_next_scene(app->scene_manager, LockTesterSceneConnected);
            consumed = true;
        } else if(event.event == LockTesterCustomEventGattError) {
            // Still navigate to connected view, just without service data
            scene_manager_next_scene(app->scene_manager, LockTesterSceneConnected);
            consumed = true;
        }
    }
    return consumed;
}

void lock_tester_scene_connecting_on_exit(void* context) {
    LockTesterApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
