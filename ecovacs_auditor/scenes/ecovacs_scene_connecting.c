#include "../ecovacs_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100

typedef enum {
    EcoConnecting,
    EcoDiscoverServices,
    EcoDiscoverChars,
} EcoConnPhase;

static EcoConnPhase conn_phase;

void ecovacs_scene_connecting_on_enter(void* context) {
    EcovacsAuditorApp* app = context;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup,
        app->current_device.has_name ? app->current_device.name : "Device",
        64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, EcovacsViewPopup);

    app->connected = false;
    app->connect_poll_count = 0;
    app->has_ecovacs_svc = false;
    app->cmd_handle = 0;
    app->rsp_handle = 0;
    conn_phase = EcoConnecting;

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

bool ecovacs_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    EcovacsAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == EcovacsCustomEventTick) {
        app->connect_poll_count++;

        if(conn_phase == EcoConnecting) {
            GapState state = gap_get_state();
            if(state == GapStateConnected) {
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                FURI_LOG_I(TAG, "Connected, handle=%d", app->connection_handle);

                popup_set_text(app->popup, "Discovering services...", 64, 36, AlignCenter, AlignCenter);
                conn_phase = EcoDiscoverServices;
                ble_gatt_client_discover_services(app->connection_handle);
            } else if(app->connect_poll_count >= CONNECT_TIMEOUT_POLLS) {
                furi_timer_stop(app->timer);
                scene_manager_previous_scene(app->scene_manager);
            }
        }
        return true;
    }

    if(event.event == EcovacsCustomEventServicesDiscovered) {
        // Look for service 0x8888
        uint8_t svc_idx = 0;
        for(uint8_t i = 0; i < app->service_count; i++) {
            if(app->services[i].uuid_type == 1 &&
               app->services[i].uuid_16 == ECOVACS_SVC_UUID) {
                app->has_ecovacs_svc = true;
                svc_idx = i;
                FURI_LOG_I(TAG, "Found Ecovacs service 0x8888 at idx=%d", i);
                break;
            }
        }

        if(!app->has_ecovacs_svc) {
            furi_timer_stop(app->timer);
            popup_set_header(app->popup, "Not Ecovacs", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "Service 0x8888 not found.\nNot an Ecovacs robot.", 64, 40, AlignCenter, AlignCenter);
        } else {
            popup_set_text(app->popup, "Found 0x8888!\nDiscovering chars...", 64, 36, AlignCenter, AlignCenter);
            conn_phase = EcoDiscoverChars;
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[svc_idx]);
        }
        return true;
    }

    if(event.event == EcovacsCustomEventCharsDiscovered) {
        for(uint8_t i = 0; i < app->char_count; i++) {
            if(app->chars[i].uuid_type == 1) {
                if(app->chars[i].uuid_16 == ECOVACS_CMD_CHAR_UUID) {
                    app->cmd_handle = app->chars[i].value_handle;
                    FURI_LOG_I(TAG, "CMD handle (0xFF02)=%d", app->cmd_handle);
                }
                if(app->chars[i].uuid_16 == ECOVACS_RSP_CHAR_UUID) {
                    app->rsp_handle = app->chars[i].value_handle;
                    FURI_LOG_I(TAG, "RSP handle (0xFF01)=%d", app->rsp_handle);
                }
            }
        }

        furi_timer_stop(app->timer);

        if(app->cmd_handle && app->rsp_handle) {
            scene_manager_next_scene(app->scene_manager, EcovacsSceneAudit);
        } else {
            popup_set_header(app->popup, "Incomplete", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "Service found but\nmissing FF01/FF02 chars.", 64, 40, AlignCenter, AlignCenter);
        }
        return true;
    }

    if(event.event == EcovacsCustomEventGattError) {
        furi_timer_stop(app->timer);
        popup_set_header(app->popup, "GATT Error", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(app->popup, "Discovery failed.", 64, 40, AlignCenter, AlignCenter);
        return true;
    }

    return false;
}

void ecovacs_scene_connecting_on_exit(void* context) {
    EcovacsAuditorApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
