#include "ecovacs_app_i.h"

static bool ecovacs_custom_event_callback(void* context, uint32_t event) {
    return scene_manager_handle_custom_event(((EcovacsAuditorApp*)context)->scene_manager, event);
}

static bool ecovacs_back_event_callback(void* context) {
    return scene_manager_handle_back_event(((EcovacsAuditorApp*)context)->scene_manager);
}

bool ecovacs_parse_adv_name(const uint8_t* data, uint8_t data_len, char* name, size_t name_size) {
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

void ecovacs_scan_callback(GapScanResultData* result, void* context) {
    EcovacsAuditorApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(!app->scan_devices[i].has_name && result->data && result->data_len > 0) {
                app->scan_devices[i].has_name = ecovacs_parse_adv_name(
                    result->data, result->data_len,
                    app->scan_devices[i].name, ECOVACS_DEVICE_NAME_LEN);
                if(app->scan_devices[i].has_name) {
                    app->scan_devices[i].is_ecovacs =
                        ecovacs_is_known_name(app->scan_devices[i].name);
                }
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    if(app->scan_device_count < ECOVACS_MAX_SCAN_DEVICES) {
        EcovacsDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = false;
        dev->is_ecovacs = false;
        dev->name[0] = '\0';
        if(result->data && result->data_len > 0) {
            dev->has_name = ecovacs_parse_adv_name(
                result->data, result->data_len, dev->name, ECOVACS_DEVICE_NAME_LEN);
            if(dev->has_name) {
                dev->is_ecovacs = ecovacs_is_known_name(dev->name);
            }
        }
        app->scan_device_count++;
    }

    furi_mutex_release(app->mutex);
}

static void ecovacs_gatt_callback(BleGattClientEvent* event, void* context) {
    EcovacsAuditorApp* app = context;

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count =
            (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
                BLE_GATT_CLIENT_MAX_SERVICES : event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, EcovacsCustomEventServicesDiscovered);
        break;
    case BleGattClientEventCharDiscoverComplete:
        app->char_count =
            (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
                BLE_GATT_CLIENT_MAX_CHARS : event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, EcovacsCustomEventCharsDiscovered);
        break;
    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, EcovacsCustomEventWriteComplete);
        break;
    case BleGattClientEventNotification:
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->resp_len = (event->notification.data_len > ECOVACS_MAX_PAYLOAD) ?
                            ECOVACS_MAX_PAYLOAD : event->notification.data_len;
        memcpy(app->resp_buf, event->notification.data, app->resp_len);
        app->has_response = true;
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, EcovacsCustomEventNotification);
        break;
    case BleGattClientEventReadComplete:
        break;
    case BleGattClientEventError:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, EcovacsCustomEventGattError);
        break;
    }
}

static void ecovacs_timer_callback(void* context) {
    EcovacsAuditorApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EcovacsCustomEventTick);
}

EcovacsAuditorApp* ecovacs_app_alloc(void) {
    EcovacsAuditorApp* app = malloc(sizeof(EcovacsAuditorApp));
    memset(app, 0, sizeof(EcovacsAuditorApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&ecovacs_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, ecovacs_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, ecovacs_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EcovacsViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EcovacsViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EcovacsViewPopup, popup_get_view(app->popup));
    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EcovacsViewLoading, loading_get_view(app->loading));
    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EcovacsViewTextBox, text_box_get_view(app->text_box));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->timer = furi_timer_alloc(ecovacs_timer_callback, FuriTimerTypePeriodic, app);
    app->audit_log = furi_string_alloc();

    ble_gatt_client_init();
    ble_gatt_client_set_callback(0, ecovacs_gatt_callback, app);

    return app;
}

void ecovacs_app_free(EcovacsAuditorApp* app) {
    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(0, NULL, NULL);

    if(app->scanning) gap_stop_scanning();
    if(app->connected) gap_disconnect(app->connection_handle);

    furi_timer_free(app->timer);
    furi_string_free(app->audit_log);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, EcovacsViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, EcovacsViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, EcovacsViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, EcovacsViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, EcovacsViewTextBox);
    text_box_free(app->text_box);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
}

int32_t ecovacs_auditor_app(void* p) {
    UNUSED(p);
    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) gap_stop_scanning();

    EcovacsAuditorApp* app = ecovacs_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, EcovacsSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    ecovacs_app_free(app);
    return 0;
}
