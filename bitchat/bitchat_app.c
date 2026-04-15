#include "bitchat_app_i.h"
#include "crypto/ed25519_donna/ed25519.h"

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

    // Rebuild chat log string — newest first so scroll reset shows latest
    furi_string_reset(app->chat_log);
    for(int i = app->message_count - 1; i >= 0; i--) {
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
    case BleGattClientEventNotification: {
        uint16_t offset = event->notification.offset;
        uint16_t len = event->notification.data_len;
        bool is_first = (offset & 0x8000) != 0;
        uint16_t real_offset = offset & 0x7FFF;

        if(len == 0) break; // ignore empty notifications

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        if(is_first) {
            // First fragment — start reassembly
            app->rx_len = 0;
        }

        // Append fragment at correct offset (with bounds check)
        if(real_offset + len <= sizeof(app->rx_buf)) {
            memcpy(&app->rx_buf[real_offset], event->notification.data, len);
            uint16_t end = real_offset + len;
            if(end > app->rx_len) app->rx_len = end;
        }

        furi_mutex_release(app->mutex);

        if(!is_first && app->rx_len > 0) {
            // Last fragment received — process complete packet
            FURI_LOG_I(TAG, "Reassembled %d bytes, first: %02X %02X %02X %02X",
                app->rx_len, app->rx_buf[0], app->rx_buf[1], app->rx_buf[2], app->rx_buf[3]);
            view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventNotification);
        }
        break;
    }
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

static void bc_app_sign_wrapper(const uint8_t* data, uint16_t len, uint8_t* sig, void* ctx) {
    bc_identity_sign((const BcIdentity*)ctx, data, len, sig);
}

// ── Peripheral service callback (data received from phone) ───────────

static uint16_t bitchat_svc_data_callback(BitchatServiceEvent event, void* context) {
    BitchatApp* app = context;
    if(event.event == BitchatServiceEventDataReceived && event.data.size > 0) {
        // Copy into rx_buf (protected by mutex) and send notification event
        if(event.data.size <= sizeof(app->rx_buf)) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            memcpy(app->rx_buf, event.data.buffer, event.data.size);
            app->rx_len = event.data.size;
            furi_mutex_release(app->mutex);
            view_dispatcher_send_custom_event(
                app->view_dispatcher, BitchatCustomEventNotification);
        }
    } else if(event.event == BitchatServiceEventPeerSubscribed) {
        // Peer subscribed to our notifications — immediately send announce
        if(app->announce_pkt_len > 0 && app->svc) {
            FURI_LOG_I(TAG, "Sending announce to new subscriber (%d bytes)", app->announce_pkt_len);
            ble_svc_bitchat_tx(app->svc, app->announce_pkt, app->announce_pkt_len);
        }
    }
    return 512;
}

// ── Alloc / Free ─────────────────────────────────────────────────────

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

    // Load or generate Ed25519 identity (keypair + peer ID)
    bc_identity_load_or_create(&app->identity);
    strncpy(app->nickname, "Flipper", BC_MAX_NICKNAME);

    // Capture our BLE MAC for dual-role tie-breaking
    const uint8_t* ble_mac = furi_hal_version_get_ble_mac();
    if(ble_mac) memcpy(app->our_mac, ble_mac, 6);

    // Init fragment and dedup tables
    bc_fragment_init(&app->frag_table);
    bc_dedup_init(&app->dedup_table);

    // Self-test: verify our Ed25519 implementation produces valid signatures
    {
        uint8_t test_msg[] = "BitChat self-test";
        uint8_t test_sig[64];
        bc_identity_sign(&app->identity, test_msg, sizeof(test_msg) - 1, test_sig);
        int verify_result = ed25519_sign_open(
            test_msg, sizeof(test_msg) - 1,
            app->identity.ed25519_public, test_sig);
        FURI_LOG_I(TAG, "Ed25519 self-test: %s", verify_result == 0 ? "PASS" : "FAIL");
        if(verify_result != 0) {
            FURI_LOG_E(TAG, "Ed25519 signatures will not verify!");
        }
    }

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

    // Pre-build signed announce packet (sent immediately when peers subscribe)
    {
        BcAnnounce announce = {0};
        strncpy(announce.nickname, app->nickname, BC_MAX_NICKNAME);
        memcpy(announce.noise_pubkey, app->identity.noise_public, 32);
        announce.has_noise_key = true;
        memcpy(announce.ed25519_pubkey, app->identity.ed25519_public, 32);
        announce.has_ed25519_key = true;

        app->announce_pkt_len = bc_build_signed_announce_packet(
            app->announce_pkt, sizeof(app->announce_pkt),
            app->identity.peer_id, &announce,
            bc_app_sign_wrapper, &app->identity);
        FURI_LOG_I(TAG, "Pre-built announce: %d bytes", app->announce_pkt_len);
    }

    // Start advertising (use HAL wrapper — gap_* functions not exported for FAPs)
    furi_hal_bt_start_advertising();

    FURI_LOG_I(TAG, "BitChat profile active, advertising started");

    return app;
}

void bitchat_app_free(BitchatApp* app) {
    // Stop timer first — prevent events during cleanup
    furi_timer_stop(app->timer);

    // Clean up BLE callbacks and GATT client
    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_deinit(); // Must unregister before profile restore or BLE core hangs
    if(app->scanning) { gap_stop_scanning(); app->scanning = false; }
    app->connected = false;

    // Stop advertising, disconnect, and restore default profile
    furi_hal_bt_stop_advertising();
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);

    // Wait for 2nd core to settle before profile swap
    furi_delay_ms(200);

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
