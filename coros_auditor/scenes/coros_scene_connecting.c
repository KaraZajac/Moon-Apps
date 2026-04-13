#include "../coros_auditor_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100

static uint8_t disc_svc_idx;

void coros_scene_connecting_on_enter(void* context) {
    CorosAuditorApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->current_device.has_name ? app->current_device.name : "Device", 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, CorosViewPopup);

    app->connected = false; app->connect_poll_count = 0;
    app->has_cmd_svc = false; app->has_notif_svc = false;
    app->cmd_write_handle = 0; app->notif_write_handle = 0;
    app->battery_handle = 0; app->model_handle = 0;
    app->serial_handle = 0; app->sw_rev_handle = 0;
    disc_svc_idx = 0;

    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) { furi_delay_ms(50); if(gap_get_state() != GapStateConnected) break; }
    }
    if(!gap_connect(app->current_device.address_type, app->current_device.address)) {
        scene_manager_previous_scene(app->scene_manager); return;
    }
    furi_timer_start(app->timer, 100);
}

bool coros_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    CorosAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == CorosCustomEventTick) {
        app->connect_poll_count++;
        if(gap_get_state() == GapStateConnected && !app->connected) {
            app->connected = true;
            app->connection_handle = gap_get_connection_handle();
            popup_set_text(app->popup, "Discovering services...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_services(app->connection_handle);
        } else if(app->connect_poll_count >= CONNECT_TIMEOUT_POLLS) {
            furi_timer_stop(app->timer);
            scene_manager_previous_scene(app->scene_manager);
        }
        return true;
    }

    if(event.event == CorosCustomEventServicesDiscovered) {
        disc_svc_idx = 0;
        if(app->service_count > 0) {
            popup_set_text(app->popup, "Enumerating chars...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_characteristics(app->connection_handle, &app->services[disc_svc_idx]);
        } else {
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, CorosSceneAudit);
        }
        return true;
    }

    if(event.event == CorosCustomEventCharsDiscovered) {
        // Identify COROS chars from this service
        for(uint8_t i = 0; i < app->char_count; i++) {
            // Check 128-bit UUID chars
            if(app->chars[i].uuid_type == 2) {
                if(coros_match_cmd_write(app->chars[i].uuid_128))
                    app->cmd_write_handle = app->chars[i].value_handle;
                if(coros_match_notif_write(app->chars[i].uuid_128))
                    app->notif_write_handle = app->chars[i].value_handle;
            }
            // Check 16-bit UUID chars
            if(app->chars[i].uuid_type == 1) {
                switch(app->chars[i].uuid_16) {
                case COROS_CHAR_BATTERY_LEVEL: app->battery_handle = app->chars[i].value_handle; break;
                case COROS_CHAR_MODEL_NUMBER: app->model_handle = app->chars[i].value_handle; break;
                case COROS_CHAR_SERIAL_NUMBER: app->serial_handle = app->chars[i].value_handle; break;
                case COROS_CHAR_SW_REVISION: app->sw_rev_handle = app->chars[i].value_handle; break;
                default: break;
                }
            }
        }

        // Also check if this service is a COROS proprietary one
        if(app->services[disc_svc_idx].uuid_type == 2) {
            if(coros_match_cmd_svc(app->services[disc_svc_idx].uuid_128))
                app->has_cmd_svc = true;
            if(coros_match_notif_svc(app->services[disc_svc_idx].uuid_128))
                app->has_notif_svc = true;
        }

        disc_svc_idx++;
        if(disc_svc_idx < app->service_count) {
            ble_gatt_client_discover_characteristics(app->connection_handle, &app->services[disc_svc_idx]);
        } else {
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, CorosSceneAudit);
        }
        return true;
    }

    if(event.event == CorosCustomEventGattError) {
        disc_svc_idx++;
        if(disc_svc_idx < app->service_count)
            ble_gatt_client_discover_characteristics(app->connection_handle, &app->services[disc_svc_idx]);
        else { furi_timer_stop(app->timer); scene_manager_next_scene(app->scene_manager, CorosSceneAudit); }
        return true;
    }
    return false;
}

void coros_scene_connecting_on_exit(void* context) {
    CorosAuditorApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
