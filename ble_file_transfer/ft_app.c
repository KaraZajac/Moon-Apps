#include "ft_app_i.h"

static bool ft_custom_event_callback(void* ctx, uint32_t event) {
    return scene_manager_handle_custom_event(((FtApp*)ctx)->scene_manager, event);
}

static bool ft_back_event_callback(void* ctx) {
    return scene_manager_handle_back_event(((FtApp*)ctx)->scene_manager);
}

// L2CAP CoC callback — runs on BLE thread
static void ft_coc_callback(BleL2capCocEvent* event, void* context) {
    FtApp* app = context;
    switch(event->type) {
    case BleL2capCocEventConnected:
        app->coc_channel_index = event->channel_index;
        app->coc_connected = true;
        app->tx_credits = event->connected.initial_credits;
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocConnected);
        break;
    case BleL2capCocEventDisconnected:
        app->coc_connected = false;
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocDisconnected);
        break;
    case BleL2capCocEventDataReceived:
        // Process received data directly (small packets, fast processing)
        if(app->mode == FtModeReceiving && app->rx_file) {
            const uint8_t* data = event->data.data;
            uint16_t len = event->data.data_len;

            if(len > 0) {
                uint8_t pkt_type = data[0];
                if(pkt_type == FT_PKT_FILE_HEADER && len >= 6) {
                    // Parse file header: [type][size_le32][filename...]
                    app->file_size = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
                    uint16_t name_len = len - 5;
                    if(name_len >= FT_MAX_FILENAME) name_len = FT_MAX_FILENAME - 1;
                    memcpy(app->filename, &data[5], name_len);
                    app->filename[name_len] = '\0';
                    app->bytes_transferred = 0;

                    // Open file for writing
                    FuriString* path = furi_string_alloc();
                    furi_string_printf(path, "/ext/received/%s", app->filename);

                    storage_simply_mkdir(app->storage, "/ext/received");
                    if(app->rx_file) {
                        storage_file_close(app->rx_file);
                    }
                    storage_file_open(
                        app->rx_file, furi_string_get_cstr(path),
                        FSAM_WRITE, FSOM_CREATE_ALWAYS);
                    furi_string_free(path);

                    FURI_LOG_I(TAG, "Receiving: %s (%lu bytes)", app->filename, app->file_size);

                    // Grant more credits for data streaming
                    ble_l2cap_coc_flow_control(app->coc_channel_index, FT_CREDITS);

                } else if(pkt_type == FT_PKT_FILE_DATA && len > 1) {
                    // Write data to file
                    uint16_t written = storage_file_write(app->rx_file, &data[1], len - 1);
                    app->bytes_transferred += written;

                    // Grant a credit for each received packet
                    ble_l2cap_coc_flow_control(app->coc_channel_index, 1);

                } else if(pkt_type == FT_PKT_FILE_DONE) {
                    // File complete
                    storage_file_close(app->rx_file);
                    app->transfer_complete = true;
                    FURI_LOG_I(TAG, "File received: %lu bytes", app->bytes_transferred);

                    // Send ACK
                    uint8_t ack = FT_PKT_FILE_ACK;
                    ble_l2cap_coc_send(app->coc_channel_index, &ack, 1);
                }
            }
        }
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocDataReceived);
        break;
    case BleL2capCocEventCreditsReceived:
        app->tx_credits += event->credits.credits;
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocCredits);
        break;
    case BleL2capCocEventTxDone:
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocTxDone);
        break;
    case BleL2capCocEventError:
        app->transfer_error = true;
        view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventCocError);
        break;
    }
}

// Timer callback
static void ft_timer_callback(void* context) {
    FtApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FtCustomEventTick);
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

    // Init L2CAP CoC
    ble_l2cap_coc_init();
    ble_l2cap_coc_set_callback(ft_coc_callback, app);

    return app;
}

void ft_app_free(FtApp* app) {
    // Clean up BLE
    gap_set_scan_callback(NULL, NULL);
    ble_l2cap_coc_set_callback(NULL, NULL);
    ble_l2cap_coc_deinit();

    if(app->scanning) {
        gap_stop_scanning();
        app->scanning = false;
    }
    if(app->coc_connected) {
        ble_l2cap_coc_disconnect(app->coc_channel_index);
    }

    // Stop extended advertising if started
    gap_ext_adv_stop(FT_ADV_HANDLE);
    gap_ext_adv_remove(FT_ADV_HANDLE);

    // Close files
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
