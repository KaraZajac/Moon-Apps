#include "../race_auditor_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100

typedef enum {
    ConnPhaseConnecting,
    ConnPhaseDiscoverServices,
    ConnPhaseDiscoverChars,
    ConnPhaseDone,
} ConnPhase;

static ConnPhase conn_phase;

void race_scene_connecting_on_enter(void* context) {
    RaceAuditorApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup,
        app->current_device.has_name ? app->current_device.name : "Device",
        64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, RaceViewPopup);

    app->connected = false;
    app->connect_poll_count = 0;
    app->variant = RaceVariantNone;
    app->tx_handle = 0;
    app->rx_handle = 0;
    app->service_count = 0;
    app->char_count = 0;
    conn_phase = ConnPhaseConnecting;

    // Disconnect any existing connection
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

bool race_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    RaceAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == RaceCustomEventTick) {
        app->connect_poll_count++;

        if(conn_phase == ConnPhaseConnecting) {
            GapState state = gap_get_state();
            if(state == GapStateConnected) {
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                FURI_LOG_I(TAG, "Connected, handle=%d", app->connection_handle);

                gap_pair(app->connection_handle, false);

                popup_set_text(app->popup, "Discovering services...", 64, 36, AlignCenter, AlignCenter);
                conn_phase = ConnPhaseDiscoverServices;
                ble_gatt_client_discover_services(app->connection_handle);
            } else if(app->connect_poll_count >= CONNECT_TIMEOUT_POLLS) {
                furi_timer_stop(app->timer);
                scene_manager_previous_scene(app->scene_manager);
            }
        }
        return true;
    }

    if(event.event == RaceCustomEventServicesDiscovered) {
        // Search for RACE service
        for(uint8_t i = 0; i < app->service_count; i++) {
            if(app->services[i].uuid_type == 2) { // 128-bit UUID
                RaceVariant v = race_match_service_uuid(app->services[i].uuid_128);
                if(v != RaceVariantNone) {
                    app->variant = v;
                    app->race_service_idx = i;
                    FURI_LOG_I(TAG, "Found RACE service (variant=%d) at idx=%d", v, i);
                    break;
                }
            }
        }

        if(app->variant == RaceVariantNone) {
            // Not an Airoha device
            furi_timer_stop(app->timer);
            popup_set_header(app->popup, "Not Vulnerable", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "No RACE service found.\nNot an Airoha device.", 64, 40, AlignCenter, AlignCenter);
            // Stay on popup, user presses back
        } else {
            popup_set_text(app->popup, "RACE found!\nDiscovering chars...", 64, 36, AlignCenter, AlignCenter);
            conn_phase = ConnPhaseDiscoverChars;
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[app->race_service_idx]);
        }
        return true;
    }

    if(event.event == RaceCustomEventCharsDiscovered) {
        // Find TX and RX characteristics
        for(uint8_t i = 0; i < app->char_count; i++) {
            if(app->chars[i].uuid_type == 2) {
                if(race_is_tx_char(app->chars[i].uuid_128, app->variant)) {
                    app->tx_handle = app->chars[i].value_handle;
                    FURI_LOG_I(TAG, "TX handle=%d", app->tx_handle);
                }
                if(race_is_rx_char(app->chars[i].uuid_128, app->variant)) {
                    app->rx_handle = app->chars[i].value_handle;
                    FURI_LOG_I(TAG, "RX handle=%d", app->rx_handle);
                }
            }
        }

        furi_timer_stop(app->timer);

        if(app->tx_handle && app->rx_handle) {
            // Ready to audit
            scene_manager_next_scene(app->scene_manager, RaceSceneAudit);
        } else {
            popup_set_header(app->popup, "Error", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "RACE service found but\nmissing TX/RX chars.", 64, 40, AlignCenter, AlignCenter);
        }
        return true;
    }

    if(event.event == RaceCustomEventGattError) {
        furi_timer_stop(app->timer);
        popup_set_header(app->popup, "GATT Error", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(app->popup, "Service discovery failed.", 64, 40, AlignCenter, AlignCenter);
        return true;
    }

    return false;
}

void race_scene_connecting_on_exit(void* context) {
    RaceAuditorApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
