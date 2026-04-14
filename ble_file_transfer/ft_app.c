#include "ft_app_i.h"

static bool ft_custom_event_callback(void* ctx, uint32_t event) {
    return scene_manager_handle_custom_event(((FtApp*)ctx)->scene_manager, event);
}

static bool ft_back_event_callback(void* ctx) {
    return scene_manager_handle_back_event(((FtApp*)ctx)->scene_manager);
}

// L2CAP CoC callback — runs on BLE thread, use mutex for shared state
static void ft_coc_callback(BleL2capCocEvent* event, void* context) {
    FtApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    switch(event->type) {
    case BleL2capCocEventConnected:
        if(app->mode == FtModeSending) {
            // Sender initiated this CoC — central role
            app->send_coc_channel = event->channel_index;
            app->send_coc_connected = true;
            app->tx_credits = event->connected.initial_credits;
        } else {
            // Incoming CoC from another Flipper — peripheral role (receive)
            app->recv_coc_channel = event->channel_index;
            app->recv_coc_connected = true;
            // Capture connection handle for the peripheral connection
            app->recv_handle = gap_get_connection_handle_by_role(false);
            if(app->recv_handle == 0) {
                app->recv_handle = gap_get_connection_handle();
            }
            app->mode = FtModeReceiving;
        }
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocConnected);
        return;

    case BleL2capCocEventDisconnected:
        if(event->channel_index == app->send_coc_channel) {
            app->send_coc_connected = false;
        } else {
            app->recv_coc_connected = false;
        }
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocDisconnected);
        return;

    case BleL2capCocEventDataReceived:
        if(app->mode == FtModeReceiving) {
            const uint8_t* data = event->data.data;
            uint16_t len = event->data.data_len;

            if(len > 0) {
                uint8_t pkt_type = data[0];
                if(pkt_type == FT_PKT_FILE_HEADER && len >= 6) {
                    app->file_size = data[1] | (data[2] << 8) |
                                     (data[3] << 16) | (data[4] << 24);
                    uint16_t name_len = len - 5;
                    if(name_len >= FT_MAX_FILENAME) name_len = FT_MAX_FILENAME - 1;
                    memcpy(app->filename, &data[5], name_len);
                    app->filename[name_len] = '\0';
                    app->bytes_transferred = 0;

                    // Open file for writing
                    storage_simply_mkdir(app->storage, "/ext/received");
                    if(app->rx_file && storage_file_is_open(app->rx_file)) {
                        storage_file_close(app->rx_file);
                    }
                    FuriString* path = furi_string_alloc();
                    furi_string_printf(path, "/ext/received/%s", app->filename);
                    if(!storage_file_open(
                           app->rx_file, furi_string_get_cstr(path),
                           FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                        FURI_LOG_E(TAG, "Failed to open rx file");
                        app->transfer_error = true;
                    }
                    furi_string_free(path);

                    FURI_LOG_I(TAG, "Receiving: %s (%lu bytes)", app->filename, app->file_size);
                    ble_l2cap_coc_flow_control(app->recv_coc_channel, FT_CREDITS);

                } else if(pkt_type == FT_PKT_FILE_DATA && len > 1) {
                    if(app->rx_file && storage_file_is_open(app->rx_file)) {
                        uint16_t written = storage_file_write(app->rx_file, &data[1], len - 1);
                        app->bytes_transferred += written;
                        if(written != (uint16_t)(len - 1)) {
                            FURI_LOG_E(TAG, "Write incomplete: %d/%d", written, len - 1);
                        }
                    }
                    ble_l2cap_coc_flow_control(app->recv_coc_channel, 1);

                } else if(pkt_type == FT_PKT_FILE_DONE) {
                    if(app->rx_file && storage_file_is_open(app->rx_file)) {
                        storage_file_close(app->rx_file);
                    }
                    app->transfer_complete = true;
                    FURI_LOG_I(TAG, "File received: %lu bytes", app->bytes_transferred);

                    uint8_t ack = FT_PKT_FILE_ACK;
                    ble_l2cap_coc_send(app->recv_coc_channel, &ack, 1);
                }
            }
        }
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocDataReceived);
        return;

    case BleL2capCocEventCreditsReceived:
        app->tx_credits += event->credits.credits;
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocCredits);
        return;

    case BleL2capCocEventTxDone:
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocTxDone);
        return;

    case BleL2capCocEventError:
        app->transfer_error = true;
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocError);
        return;
    }

    furi_mutex_release(app->mutex);
}

static void ft_timer_callback(void* context) {
    FtApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventTick);
}

// Start extended advertising with FT service UUID (always-on receiver)
void ft_start_advertising(FtApp* app) {
    static const uint8_t ft_svc_uuid[] = FT_SVC_UUID_128;

    gap_ext_adv_configure(
        FT_ADV_HANDLE,
        GAP_EXT_ADV_PROP_CONNECTABLE | GAP_EXT_ADV_PROP_SCANNABLE,
        0x0080, 0x00A0, 0x01, 0x01);

    uint8_t adv_data[20];
    adv_data[0] = 17;
    adv_data[1] = 0x07;
    memcpy(&adv_data[2], ft_svc_uuid, 16);
    gap_ext_adv_set_data(FT_ADV_HANDLE, adv_data, 18);

    gap_ext_adv_start(FT_ADV_HANDLE, 0);
    app->adv_active = true;
    FURI_LOG_I(TAG, "Receiver advertising started");
}

void ft_stop_advertising(FtApp* app) {
    if(app->adv_active) {
        gap_ext_adv_stop(FT_ADV_HANDLE);
        app->adv_active = false;
    }
}

FtApp* ft_app_alloc(void) {
    FtApp* app = malloc(sizeof(FtApp));
    memset(app, 0, sizeof(FtApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&ft_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, ft_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, ft_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FtViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FtViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FtViewPopup, popup_get_view(app->popup));
    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FtViewLoading, loading_get_view(app->loading));
    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FtViewDialogEx, dialog_ex_get_view(app->dialog_ex));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->timer = furi_timer_alloc(ft_timer_callback, FuriTimerTypePeriodic, app);
    app->file_path = furi_string_alloc();
    app->rx_file = storage_file_alloc(app->storage);
    app->tx_file = storage_file_alloc(app->storage);

    ble_l2cap_coc_init();
    ble_l2cap_coc_set_callback(ft_coc_callback, app);

    // Start advertising immediately — always ready to receive
    ft_start_advertising(app);

    return app;
}

void ft_app_free(FtApp* app) {
    ft_stop_advertising(app);
    gap_ext_adv_remove(FT_ADV_HANDLE);

    gap_set_scan_callback(NULL, NULL);
    ble_l2cap_coc_set_callback(NULL, NULL);
    ble_l2cap_coc_deinit();

    if(app->scanning) { gap_stop_scanning(); app->scanning = false; }
    if(app->send_coc_connected) ble_l2cap_coc_disconnect(app->send_coc_channel);
    if(app->recv_coc_connected) ble_l2cap_coc_disconnect(app->recv_coc_channel);
    if(app->send_connected) gap_disconnect(app->send_handle);

    if(storage_file_is_open(app->rx_file)) storage_file_close(app->rx_file);
    if(storage_file_is_open(app->tx_file)) storage_file_close(app->tx_file);
    storage_file_free(app->rx_file);
    storage_file_free(app->tx_file);

    furi_timer_free(app->timer);
    furi_string_free(app->file_path);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, FtViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, FtViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, FtViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, FtViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, FtViewDialogEx);
    dialog_ex_free(app->dialog_ex);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    free(app);
}

int32_t ble_file_transfer_app(void* p) {
    UNUSED(p);
    FtApp* app = ft_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, FtSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    ft_app_free(app);
    return 0;
}
