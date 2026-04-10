#include "bitchat_app_i.h"

static bool bitchat_custom_event_callback(void* ctx, uint32_t event) {
    return scene_manager_handle_custom_event(((BitchatApp*)ctx)->scene_manager, event);
}

static bool bitchat_back_event_callback(void* ctx) {
    return scene_manager_handle_back_event(((BitchatApp*)ctx)->scene_manager);
}

// ── Chat message helpers ─────────────────────────────────────────────

void bitchat_add_chat_message(BitchatApp* app, const char* sender, const char* content) {
    // Shift messages if full
    if(app->message_count >= BC_MAX_MESSAGES) {
        memmove(&app->messages[0], &app->messages[1],
                (BC_MAX_MESSAGES - 1) * sizeof(BcChatLine));
        app->message_count = BC_MAX_MESSAGES - 1;
    }

    BcChatLine* line = &app->messages[app->message_count];
    strncpy(line->sender, sender, BC_MAX_NICKNAME);
    line->sender[BC_MAX_NICKNAME] = '\0';
    strncpy(line->content, content, BC_MAX_MSG_CONTENT);
    line->content[BC_MAX_MSG_CONTENT] = '\0';
    app->message_count++;

    // Rebuild chat log string
    furi_string_reset(app->chat_log);
    for(uint8_t i = 0; i < app->message_count; i++) {
        furi_string_cat_printf(
            app->chat_log, "%s: %s\n", app->messages[i].sender, app->messages[i].content);
    }
}

// ── GATT callback ────────────────────────────────────────────────────

void bitchat_gatt_callback(BleGattClientEvent* event, void* context) {
    BitchatApp* app = context;
    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count = (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
            BLE_GATT_CLIENT_MAX_SERVICES : event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventServicesDiscovered);
        break;
    case BleGattClientEventCharDiscoverComplete:
        app->char_count = (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
            BLE_GATT_CLIENT_MAX_CHARS : event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventCharsDiscovered);
        break;
    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventWriteComplete);
        break;
    case BleGattClientEventNotification:
        // Received data from a peer
        if(event->notification.data_len <= sizeof(app->rx_buf)) {
            memcpy(app->rx_buf, event->notification.data, event->notification.data_len);
            app->rx_len = event->notification.data_len;
        }
        view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventNotification);
        break;
    case BleGattClientEventError:
        view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventGattError);
        break;
    default:
        break;
    }
}

// ── GAP scan callback ────────────────────────────────────────────────

void bitchat_scan_callback(GapScanResultData* result, void* context) {
    BitchatApp* app = context;
    // We only use scan to find peers; actual filtering happens in the scan scene
    // by checking service UUIDs. For now just store raw results for the scan scene.
    (void)app;
    (void)result;
    // Scan scene handles connection directly via gap_connect after finding a peer
}

// ── Timer ────────────────────────────────────────────────────────────

static void bitchat_timer_callback(void* context) {
    BitchatApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventTick);
}

// ── Peripheral service callback (data received from phone) ───────────

static uint16_t bitchat_svc_data_callback(BitchatServiceEvent event, void* context) {
    BitchatApp* app = context;
    if(event.event == BitchatServiceEventDataReceived && event.data.size > 0) {
        // Copy into rx_buf and send notification event
        if(event.data.size <= sizeof(app->rx_buf)) {
            memcpy(app->rx_buf, event.data.buffer, event.data.size);
            app->rx_len = event.data.size;
            view_dispatcher_send_custom_event(
                app->view_dispatcher, BitchatCustomEventNotification);
        }
    }
    return 512;
}

// ── Alloc / Free ─────────────────────────────────────────────────────

static void generate_random_peer_id(uint8_t* peer_id) {
    for(int i = 0; i < BC_SENDER_ID_SIZE; i++) {
        peer_id[i] = rand() & 0xFF;
    }
}

BitchatApp* bitchat_app_alloc(void) {
    BitchatApp* app = malloc(sizeof(BitchatApp));
    memset(app, 0, sizeof(BitchatApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->bt = furi_record_open(RECORD_BT);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&bitchat_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, bitchat_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, bitchat_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BitchatViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BitchatViewWidget, widget_get_view(app->widget));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BitchatViewTextBox, text_box_get_view(app->text_box));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BitchatViewTextInput, text_input_get_view(app->text_input));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BitchatViewPopup, popup_get_view(app->popup));

    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BitchatViewLoading, loading_get_view(app->loading));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->chat_log = furi_string_alloc();
    app->timer = furi_timer_alloc(bitchat_timer_callback, FuriTimerTypePeriodic, app);

    // Generate random peer ID
    generate_random_peer_id(app->peer_id);
    strncpy(app->nickname, "Flipper", BC_MAX_NICKNAME);

    // Start BitChat BLE profile (replaces default Flipper BLE profile)
    // This registers our GATT service and advertises the BitChat UUID
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    app->ble_profile = bt_profile_start(app->bt, ble_profile_bitchat, NULL);
    furi_check(app->ble_profile);

    // Set up service callback for incoming peripheral writes
    ble_profile_bitchat_set_event_callback(
        app->ble_profile, 512, bitchat_svc_data_callback, app);
    app->svc = ble_profile_bitchat_get_service(app->ble_profile);

    // Start advertising
    furi_hal_bt_start_advertising();

    // NOTE: GATT client init is deferred to scan scene to avoid
    // conflicting with profile lifecycle during shutdown

    FURI_LOG_I(TAG, "BitChat profile active, advertising started");

    return app;
}

void bitchat_app_free(BitchatApp* app) {
    // Stop timer first — prevent events during cleanup
    furi_timer_stop(app->timer);

    // Clean up BLE callbacks
    gap_set_scan_callback(NULL, NULL);
    if(app->scanning) { gap_stop_scanning(); app->scanning = false; }
    app->connected = false;

    // Stop advertising, disconnect, and restore default profile
    furi_hal_bt_stop_advertising();
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);

    // Wait for 2nd core to settle before profile swap
    furi_delay_ms(500);

    bt_profile_restore_default(app->bt);

    furi_timer_free(app->timer);
    furi_string_free(app->chat_log);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, BitchatViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, BitchatViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, BitchatViewTextBox);
    text_box_free(app->text_box);
    view_dispatcher_remove_view(app->view_dispatcher, BitchatViewTextInput);
    text_input_free(app->text_input);
    view_dispatcher_remove_view(app->view_dispatcher, BitchatViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, BitchatViewLoading);
    loading_free(app->loading);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);
    free(app);
}

int32_t bitchat_app(void* p) {
    UNUSED(p);

    BitchatApp* app = bitchat_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, BitchatSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    bitchat_app_free(app);
    return 0;
}
