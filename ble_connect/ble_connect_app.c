#include "ble_connect_app_i.h"

static bool ble_connect_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    BleConnectApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool ble_connect_back_event_callback(void* context) {
    furi_assert(context);
    BleConnectApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

void ble_connect_scan_callback_fn(GapScanResultData* result, void* context) {
    BleConnectApp* app = context;
    furi_mutex_acquire(app->scan_mutex, FuriWaitForever);

    // Update existing or add new
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(!app->scan_devices[i].has_name && result->data && result->data_len > 0) {
                app->scan_devices[i].has_name = ble_connect_device_parse_adv_name(
                    result->data,
                    result->data_len,
                    app->scan_devices[i].name,
                    BLE_CONNECT_DEVICE_NAME_LEN);
            }
            furi_mutex_release(app->scan_mutex);
            return;
        }
    }

    if(app->scan_device_count < BLE_CONNECT_MAX_SCAN_DEVICES) {
        BleConnectDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = false;
        dev->name[0] = '\0';
        if(result->data && result->data_len > 0) {
            dev->has_name = ble_connect_device_parse_adv_name(
                result->data, result->data_len, dev->name, BLE_CONNECT_DEVICE_NAME_LEN);
        }
        app->scan_device_count++;
    }

    furi_mutex_release(app->scan_mutex);
}

static void ble_connect_gatt_callback(BleGattClientEvent* event, void* context) {
    BleConnectApp* app = context;

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
        app->service_count =
            (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
                BLE_GATT_CLIENT_MAX_SERVICES :
                event->discover.count;
        memcpy(app->services, event->discover.services, app->service_count * sizeof(BleGattService));
        furi_mutex_release(app->scan_mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BleConnectCustomEventServicesDiscovered);
        break;

    case BleGattClientEventCharDiscoverComplete:
        furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
        app->char_count =
            (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
                BLE_GATT_CLIENT_MAX_CHARS :
                event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars, app->char_count * sizeof(BleGattCharacteristic));
        furi_mutex_release(app->scan_mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BleConnectCustomEventCharsDiscovered);
        break;

    case BleGattClientEventReadComplete:
        furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
        app->read_len = (event->read.data_len > sizeof(app->read_buf)) ?
                            sizeof(app->read_buf) :
                            event->read.data_len;
        memcpy(app->read_buf, event->read.data, app->read_len);
        app->read_handle = event->read.value_handle;
        app->has_read_data = true;
        furi_mutex_release(app->scan_mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BleConnectCustomEventReadComplete);
        break;

    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BleConnectCustomEventWriteComplete);
        break;

    case BleGattClientEventNotification:
        furi_mutex_acquire(app->scan_mutex, FuriWaitForever);
        app->read_len = (event->notification.data_len > sizeof(app->read_buf)) ?
                            sizeof(app->read_buf) :
                            event->notification.data_len;
        memcpy(app->read_buf, event->notification.data, app->read_len);
        app->read_handle = event->notification.value_handle;
        app->has_read_data = true;
        furi_mutex_release(app->scan_mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BleConnectCustomEventNotification);
        break;

    case BleGattClientEventError:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BleConnectCustomEventGattError);
        break;
    }
}

static void ble_connect_connect_timeout_callback(void* context) {
    BleConnectApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, BleConnectCustomEventConnectTimeout);
}

BleConnectApp* ble_connect_app_alloc(void) {
    BleConnectApp* app = malloc(sizeof(BleConnectApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&ble_connect_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, ble_connect_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ble_connect_back_event_callback);

    // Allocate GUI modules
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewPopup, popup_get_view(app->popup));

    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewLoading, loading_get_view(app->loading));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewTextInput, text_input_get_view(app->text_input));

    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewByteInput, byte_input_get_view(app->byte_input));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewTextBox, text_box_get_view(app->text_box));

    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleConnectViewDialogEx, dialog_ex_get_view(app->dialog_ex));

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        BleConnectViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    // State init
    app->scan_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->scan_device_count = 0;
    app->scanning = false;
    app->connected = false;
    app->connection_handle = 0;
    app->service_count = 0;
    app->char_count = 0;
    app->has_read_data = false;
    app->text_box_store = furi_string_alloc();
    app->file_path = furi_string_alloc();

    app->connect_timer = furi_timer_alloc(
        ble_connect_connect_timeout_callback, FuriTimerTypePeriodic, app);

    // Init GATT client; per-connection callback registered once we connect
    ble_gatt_client_init();

    return app;
}

void ble_connect_register_gatt_callback(BleConnectApp* app) {
    ble_gatt_client_set_callback(app->connection_handle, ble_connect_gatt_callback, app);
}

void ble_connect_app_free(BleConnectApp* app) {
    furi_assert(app);

    // Clean up BLE state — always clear callbacks and stop any active operations
    gap_set_scan_callback(NULL, NULL);
    if(app->connection_handle) {
        ble_gatt_client_set_callback(app->connection_handle, NULL, NULL);
    }

    if(app->scanning) {
        gap_stop_scanning();
        app->scanning = false;
    }
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }


    furi_timer_free(app->connect_timer);
    furi_string_free(app->text_box_store);
    furi_string_free(app->file_path);
    furi_mutex_free(app->scan_mutex);

    // Remove views and free modules
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewTextInput);
    text_input_free(app->text_input);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewByteInput);
    byte_input_free(app->byte_input);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewTextBox);
    text_box_free(app->text_box);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewDialogEx);
    dialog_ex_free(app->dialog_ex);
    view_dispatcher_remove_view(app->view_dispatcher, BleConnectViewVariableItemList);
    variable_item_list_free(app->variable_item_list);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    free(app);
}

int32_t ble_connect_app(void* p) {
    UNUSED(p);

    // Ensure clean BLE state from any previous run
    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) {
        gap_stop_scanning();
    }

    BleConnectApp* app = ble_connect_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, BleConnectSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    ble_connect_app_free(app);
    return 0;
}
