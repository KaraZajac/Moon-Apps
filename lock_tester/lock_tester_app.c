#include "lock_tester_app_i.h"

static bool lock_tester_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    LockTesterApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool lock_tester_back_event_callback(void* context) {
    furi_assert(context);
    LockTesterApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

// GAP scan callback
void lock_tester_scan_callback(GapScanResultData* result, void* context) {
    LockTesterApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    // Update existing device
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(!app->scan_devices[i].has_name && result->data && result->data_len > 0) {
                app->scan_devices[i].has_name = lock_tester_parse_adv_name(
                    result->data, result->data_len,
                    app->scan_devices[i].name, LOCK_TESTER_DEVICE_NAME_LEN);
                // Re-check profile match if we got a name
                if(app->scan_devices[i].has_name && !app->scan_devices[i].profile) {
                    app->scan_devices[i].profile =
                        lock_profile_match_by_name(app->scan_devices[i].name);
                }
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    // Add new device
    if(app->scan_device_count < LOCK_TESTER_MAX_SCAN_DEVICES) {
        LockTesterDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = false;
        dev->name[0] = '\0';
        dev->profile = NULL;

        if(result->data && result->data_len > 0) {
            dev->has_name = lock_tester_parse_adv_name(
                result->data, result->data_len, dev->name, LOCK_TESTER_DEVICE_NAME_LEN);

            // Check for known lock by name
            if(dev->has_name) {
                dev->profile = lock_profile_match_by_name(dev->name);
            }

            // Check for known lock by service UUID in advertisement
            if(!dev->profile) {
                uint16_t svc = lock_tester_check_adv_services(result->data, result->data_len);
                if(svc) {
                    dev->profile = lock_profile_match_by_service(svc);
                }
            }
        }

        app->scan_device_count++;
    }

    furi_mutex_release(app->mutex);
}

// GATT client callback
static void lock_tester_gatt_callback(BleGattClientEvent* event, void* context) {
    LockTesterApp* app = context;

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count =
            (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
                BLE_GATT_CLIENT_MAX_SERVICES : event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, LockTesterCustomEventServicesDiscovered);
        break;

    case BleGattClientEventCharDiscoverComplete:
        app->char_count =
            (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
                BLE_GATT_CLIENT_MAX_CHARS : event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, LockTesterCustomEventCharsDiscovered);
        break;

    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, LockTesterCustomEventWriteComplete);
        break;

    case BleGattClientEventNotification:
        if(event->notification.data_len <= sizeof(app->notify_buf)) {
            memcpy(app->notify_buf, event->notification.data, event->notification.data_len);
            app->notify_len = event->notification.data_len;
            app->has_notification = true;
        }
        view_dispatcher_send_custom_event(
            app->view_dispatcher, LockTesterCustomEventNotification);
        break;

    case BleGattClientEventReadComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, LockTesterCustomEventReadComplete);
        break;

    case BleGattClientEventError:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, LockTesterCustomEventGattError);
        break;
    }
}

// Timer callback
static void lock_tester_timer_callback(void* context) {
    LockTesterApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, LockTesterCustomEventTick);
}

LockTesterApp* lock_tester_app_alloc(void) {
    LockTesterApp* app = malloc(sizeof(LockTesterApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&lock_tester_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, lock_tester_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, lock_tester_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LockTesterViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LockTesterViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LockTesterViewPopup, popup_get_view(app->popup));

    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LockTesterViewLoading, loading_get_view(app->loading));

    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LockTesterViewByteInput, byte_input_get_view(app->byte_input));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LockTesterViewTextBox, text_box_get_view(app->text_box));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->scan_device_count = 0;
    app->scanning = false;
    app->connected = false;
    app->test_running = false;
    app->has_notification = false;
    app->test_log = furi_string_alloc();

    app->timer = furi_timer_alloc(lock_tester_timer_callback, FuriTimerTypePeriodic, app);

    ble_gatt_client_init();
    ble_gatt_client_set_callback(0, lock_tester_gatt_callback, app);

    return app;
}

void lock_tester_app_free(LockTesterApp* app) {
    furi_assert(app);

    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(0, NULL, NULL);

    if(app->scanning) {
        gap_stop_scanning();
        app->scanning = false;
    }
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }

    furi_timer_free(app->timer);
    furi_string_free(app->test_log);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, LockTesterViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, LockTesterViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, LockTesterViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, LockTesterViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, LockTesterViewByteInput);
    byte_input_free(app->byte_input);
    view_dispatcher_remove_view(app->view_dispatcher, LockTesterViewTextBox);
    text_box_free(app->text_box);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t lock_tester_app(void* p) {
    UNUSED(p);

    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) {
        gap_stop_scanning();
    }

    LockTesterApp* app = lock_tester_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, LockTesterSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    lock_tester_app_free(app);
    return 0;
}
