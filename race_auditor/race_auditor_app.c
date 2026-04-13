#include "race_auditor_app_i.h"

static bool race_custom_event_callback(void* context, uint32_t event) {
    return scene_manager_handle_custom_event(((RaceAuditorApp*)context)->scene_manager, event);
}

static bool race_back_event_callback(void* context) {
    return scene_manager_handle_back_event(((RaceAuditorApp*)context)->scene_manager);
}

bool race_parse_adv_name(const uint8_t* data, uint8_t data_len, char* name, size_t name_size) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];
        if(type == 0x08 || type == 0x09) {
            uint8_t nl = len - 1;
            if(nl >= name_size) nl = name_size - 1;
            memcpy(name, &data[pos + 2], nl);
            name[nl] = '\0';
            return true;
        }
        pos += len + 1;
    }
    return false;
}

void race_scan_callback(GapScanResultData* result, void* context) {
    RaceAuditorApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(!app->scan_devices[i].has_name && result->data && result->data_len > 0) {
                app->scan_devices[i].has_name = race_parse_adv_name(
                    result->data, result->data_len,
                    app->scan_devices[i].name, RACE_DEVICE_NAME_LEN);
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    if(app->scan_device_count < RACE_MAX_SCAN_DEVICES) {
        RaceDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = false;
        dev->name[0] = '\0';
        if(result->data && result->data_len > 0) {
            dev->has_name = race_parse_adv_name(
                result->data, result->data_len, dev->name, RACE_DEVICE_NAME_LEN);
        }
        app->scan_device_count++;
    }

    furi_mutex_release(app->mutex);
}

static void race_gatt_callback(BleGattClientEvent* event, void* context) {
    RaceAuditorApp* app = context;

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count =
            (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
                BLE_GATT_CLIENT_MAX_SERVICES : event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RaceCustomEventServicesDiscovered);
        break;
    case BleGattClientEventCharDiscoverComplete:
        app->char_count =
            (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
                BLE_GATT_CLIENT_MAX_CHARS : event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RaceCustomEventCharsDiscovered);
        break;
    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RaceCustomEventWriteComplete);
        break;
    case BleGattClientEventNotification:
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->resp_len = (event->notification.data_len > RACE_MAX_RESPONSE) ?
                            RACE_MAX_RESPONSE : event->notification.data_len;
        memcpy(app->resp_buf, event->notification.data, app->resp_len);
        app->has_response = true;
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RaceCustomEventNotification);
        break;
    case BleGattClientEventReadComplete:
        break;
    case BleGattClientEventError:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RaceCustomEventGattError);
        break;
    }
}

static void race_timer_callback(void* context) {
    RaceAuditorApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RaceCustomEventTick);
}

RaceAuditorApp* race_auditor_app_alloc(void) {
    RaceAuditorApp* app = malloc(sizeof(RaceAuditorApp));
    memset(app, 0, sizeof(RaceAuditorApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&race_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, race_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, race_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RaceViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RaceViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RaceViewPopup, popup_get_view(app->popup));

    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RaceViewLoading, loading_get_view(app->loading));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RaceViewTextBox, text_box_get_view(app->text_box));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->timer = furi_timer_alloc(race_timer_callback, FuriTimerTypePeriodic, app);
    app->audit_log = furi_string_alloc();

    ble_gatt_client_init();
    ble_gatt_client_set_callback(race_gatt_callback, app);

    return app;
}

void race_auditor_app_free(RaceAuditorApp* app) {
    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(NULL, NULL);

    if(app->scanning) {
        gap_stop_scanning();
    }
    if(app->connected) {
        gap_disconnect(app->connection_handle);
    }

    furi_timer_free(app->timer);
    furi_string_free(app->audit_log);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, RaceViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, RaceViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, RaceViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, RaceViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, RaceViewTextBox);
    text_box_free(app->text_box);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t race_auditor_app(void* p) {
    UNUSED(p);

    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) {
        gap_stop_scanning();
    }

    RaceAuditorApp* app = race_auditor_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, RaceSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    race_auditor_app_free(app);
    return 0;
}
