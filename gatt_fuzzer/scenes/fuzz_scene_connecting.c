#include "../gatt_fuzzer_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100

static uint8_t disc_svc_idx;

void fuzz_scene_connecting_on_enter(void* context) {
    GattFuzzerApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Connecting...", 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup,
        app->current_device.has_name ? app->current_device.name : "Device",
        64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuzzViewPopup);

    app->connected = false;
    app->connect_poll_count = 0;
    app->service_count = 0;
    app->all_char_total = 0;
    app->discovery_done = false;
    disc_svc_idx = 0;

    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    if(!gap_connect(app->current_device.address_type, app->current_device.address)) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    furi_timer_start(app->timer, 100);
}

bool fuzz_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    GattFuzzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FuzzCustomEventTick) {
        app->connect_poll_count++;
        GapState state = gap_get_state();
        if(state == GapStateConnected && !app->connected) {
            app->connected = true;
            app->connection_handle = gap_get_connection_handle();
            gap_pair(app->connection_handle, false);
            popup_set_text(app->popup, "Discovering services...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_services(app->connection_handle);
        } else if(app->connect_poll_count >= CONNECT_TIMEOUT_POLLS) {
            furi_timer_stop(app->timer);
            scene_manager_previous_scene(app->scene_manager);
        }
        return true;
    }

    if(event.event == FuzzCustomEventServicesDiscovered) {
        FURI_LOG_I(TAG, "Found %d services", app->service_count);
        // Start discovering chars for each service
        disc_svc_idx = 0;
        if(app->service_count > 0) {
            popup_set_text(app->popup, "Enumerating chars...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[disc_svc_idx]);
        } else {
            app->discovery_done = true;
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, FuzzSceneTestMenu);
        }
        return true;
    }

    if(event.event == FuzzCustomEventCharsDiscovered) {
        // Collect all char handles from this service
        for(uint8_t i = 0; i < app->char_count && app->all_char_total < 128; i++) {
            app->all_char_handles[app->all_char_total] = app->chars[i].value_handle;
            app->all_char_properties[app->all_char_total] = app->chars[i].properties;
            app->all_char_total++;
        }

        // Move to next service
        disc_svc_idx++;
        if(disc_svc_idx < app->service_count) {
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[disc_svc_idx]);
        } else {
            app->discovery_done = true;
            furi_timer_stop(app->timer);
            FURI_LOG_I(TAG, "Total chars: %d", app->all_char_total);
            scene_manager_next_scene(app->scene_manager, FuzzSceneTestMenu);
        }
        return true;
    }

    if(event.event == FuzzCustomEventGattError) {
        // Skip this service, try next
        disc_svc_idx++;
        if(disc_svc_idx < app->service_count) {
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[disc_svc_idx]);
        } else {
            app->discovery_done = true;
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, FuzzSceneTestMenu);
        }
        return true;
    }

    return false;
}

void fuzz_scene_connecting_on_exit(void* context) {
    GattFuzzerApp* app = context;
    furi_timer_stop(app->timer);
    popup_reset(app->popup);
}
