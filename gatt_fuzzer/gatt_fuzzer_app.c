#include "gatt_fuzzer_app_i.h"

static bool fuzz_custom_event_cb(void* ctx, uint32_t event) {
    return scene_manager_handle_custom_event(((GattFuzzerApp*)ctx)->scene_manager, event);
}

static bool fuzz_back_event_cb(void* ctx) {
    return scene_manager_handle_back_event(((GattFuzzerApp*)ctx)->scene_manager);
}

bool fuzz_parse_adv_name(const uint8_t* data, uint8_t data_len, char* name, size_t name_size) {
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

void fuzz_scan_callback(GapScanResultData* result, void* context) {
    GattFuzzerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(!app->scan_devices[i].has_name && result->data && result->data_len > 0) {
                app->scan_devices[i].has_name = fuzz_parse_adv_name(
                    result->data, result->data_len,
                    app->scan_devices[i].name, FUZZ_DEVICE_NAME_LEN);
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }
    if(app->scan_device_count < FUZZ_MAX_SCAN_DEVICES) {
        FuzzDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = false;
        dev->name[0] = '\0';
        if(result->data && result->data_len > 0) {
            dev->has_name = fuzz_parse_adv_name(
                result->data, result->data_len, dev->name, FUZZ_DEVICE_NAME_LEN);
        }
        app->scan_device_count++;
    }
    furi_mutex_release(app->mutex);
}

static void fuzz_gatt_callback(BleGattClientEvent* event, void* context) {
    GattFuzzerApp* app = context;
    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count =
            (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
                BLE_GATT_CLIENT_MAX_SERVICES : event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventServicesDiscovered);
        break;
    case BleGattClientEventCharDiscoverComplete:
        app->char_count =
            (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
                BLE_GATT_CLIENT_MAX_CHARS : event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventCharsDiscovered);
        break;
    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventWriteComplete);
        break;
    case BleGattClientEventReadComplete:
        view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventReadComplete);
        break;
    case BleGattClientEventNotification:
        view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventNotification);
        break;
    case BleGattClientEventError:
        view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventGattError);
        break;
    }
}

static void fuzz_timer_callback(void* context) {
    GattFuzzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FuzzCustomEventTick);
}

GattFuzzerApp* gatt_fuzzer_app_alloc(void) {
    GattFuzzerApp* app = malloc(sizeof(GattFuzzerApp));
    memset(app, 0, sizeof(GattFuzzerApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&fuzz_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, fuzz_custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, fuzz_back_event_cb);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FuzzViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FuzzViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FuzzViewPopup, popup_get_view(app->popup));
    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FuzzViewLoading, loading_get_view(app->loading));
    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FuzzViewTextBox, text_box_get_view(app->text_box));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->timer = furi_timer_alloc(fuzz_timer_callback, FuriTimerTypePeriodic, app);
    app->fuzz_log = furi_string_alloc();
    app->rng_state = 0xDEADBEEF;

    ble_gatt_client_init();
    ble_gatt_client_set_callback(fuzz_gatt_callback, app);

    return app;
}

void gatt_fuzzer_app_free(GattFuzzerApp* app) {
    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(NULL, NULL);
    if(app->scanning) gap_stop_scanning();
    if(app->connected) gap_disconnect(app->connection_handle);

    furi_timer_free(app->timer);
    furi_string_free(app->fuzz_log);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, FuzzViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, FuzzViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, FuzzViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, FuzzViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, FuzzViewTextBox);
    text_box_free(app->text_box);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
}

int32_t gatt_fuzzer_app(void* p) {
    UNUSED(p);
    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) gap_stop_scanning();

    GattFuzzerApp* app = gatt_fuzzer_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, FuzzSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    gatt_fuzzer_app_free(app);
    return 0;
}
