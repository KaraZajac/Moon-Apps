#include "../bitchat_app_i.h"

static void bc_sign_wrapper(const uint8_t* data, uint16_t len, uint8_t* sig, void* ctx) {
    bc_identity_sign((const BcIdentity*)ctx, data, len, sig);
}

#define SCAN_TIMEOUT_MS 10000
#define CONNECT_TIMEOUT_POLLS 100

static const uint8_t bc_svc_uuid[] = BITCHAT_SVC_UUID_128;
static const uint8_t bc_char_uuid[] = BITCHAT_CHAR_UUID_128;

typedef enum {
    ScanPhaseScanning,
    ScanPhaseSelectPeer,
    ScanPhaseConnecting,
    ScanPhaseDiscoverServices,
    ScanPhaseDiscoverChars,
    ScanPhaseSubscribe,
    ScanPhaseAnnounceDelay,
    ScanPhaseDone,
} ScanPhase;

static ScanPhase scan_phase;

static bool parse_adv_name(const uint8_t* data, uint8_t len, char* name, size_t sz) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t l = data[pos];
        if(l == 0 || pos + l >= len) break;
        if(data[pos + 1] == 0x08 || data[pos + 1] == 0x09) {
            uint8_t nl = l - 1;
            if(nl >= sz) nl = sz - 1;
            memcpy(name, &data[pos + 2], nl);
            name[nl] = '\0';
            return true;
        }
        pos += l + 1;
    }
    return false;
}

// Scan callback — collect BitChat peers for selection
static void bc_scan_callback(GapScanResultData* result, void* context) {
    BitchatApp* app = context;
    if(!result->data || result->data_len == 0) return;

    // Check for BitChat service UUID in advertisement
    uint8_t pos = 0;
    bool found_uuid = false;
    while(pos < result->data_len) {
        uint8_t len = result->data[pos];
        if(len == 0 || pos + len >= result->data_len) break;
        uint8_t type = result->data[pos + 1];
        if((type == 0x06 || type == 0x07) && len >= 17) {
            for(uint8_t i = 0; i + 15 < len - 1; i += 16) {
                if(memcmp(&result->data[pos + 2 + i], bc_svc_uuid, 16) == 0) {
                    found_uuid = true;
                    break;
                }
            }
        }
        if(found_uuid) break;
        pos += len + 1;
    }
    if(!found_uuid) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    // Update existing or add new
    for(uint8_t i = 0; i < app->scan_result_count; i++) {
        if(memcmp(app->scan_results[i].address, result->address, 6) == 0) {
            app->scan_results[i].rssi = result->rssi;
            furi_mutex_release(app->mutex);
            return;
        }
    }

    if(app->scan_result_count < BC_MAX_PEERS) {
        BcPeer* dev = &app->scan_results[app->scan_result_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        char name[32] = {0};
        if(parse_adv_name(result->data, result->data_len, name, sizeof(name))) {
            strncpy(dev->nickname, name, BC_MAX_NICKNAME);
        } else {
            snprintf(dev->nickname, BC_MAX_NICKNAME, "%02X:%02X:%02X:%02X",
                result->address[3], result->address[2],
                result->address[1], result->address[0]);
        }
        app->scan_result_count++;
    }

    furi_mutex_release(app->mutex);
}

static void bc_peer_select_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((BitchatApp*)ctx)->view_dispatcher, 1000 + idx);
}

void bitchat_scene_scan_on_enter(void* context) {
    BitchatApp* app = context;

    scan_phase = ScanPhaseScanning;
    app->tick_count = 0;
    app->connected = false;
    app->bc_char_handle = 0;
    app->scan_result_count = 0;

    popup_reset(app->popup);
    popup_set_header(app->popup, "BitChat", 64, 10, AlignCenter, AlignTop);
    popup_set_text(app->popup, "Scanning for\nBitChat peers...", 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewPopup);

    extern void bitchat_gatt_callback(BleGattClientEvent* event, void* context);
    ble_gatt_client_init();
    ble_gatt_client_set_callback(bitchat_gatt_callback, app);

    gap_set_scan_callback(bc_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60, .window = 0x30, .active = true, .timeout_ms = SCAN_TIMEOUT_MS,
    };
    app->scanning = true;
    if(!gap_start_scanning(&params)) {
        app->scanning = false;
        popup_set_text(app->popup, "Scan failed", 64, 36, AlignCenter, AlignCenter);
        scan_phase = ScanPhaseDone;
    }

    furi_timer_start(app->timer, 100);
}

bool bitchat_scene_scan_on_event(void* context, SceneManagerEvent event) {
    BitchatApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != BitchatCustomEventTick)
        goto check_gatt;

    app->tick_count++;

    switch(scan_phase) {
    case ScanPhaseScanning:
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
        }
        if(!app->scanning || app->tick_count >= CONNECT_TIMEOUT_POLLS) {
            if(app->scanning) {
                gap_stop_scanning();
                gap_set_scan_callback(NULL, NULL);
                app->scanning = false;
            }

            if(app->scan_result_count == 0) {
                popup_set_text(app->popup, "No BitChat peers\nfound nearby", 64, 36, AlignCenter, AlignCenter);
                scan_phase = ScanPhaseDone;
            } else if(app->scan_result_count == 1) {
                // Single peer — connect directly
                app->selected_scan_idx = 0;
                scan_phase = ScanPhaseConnecting;
                app->tick_count = 0;
                BcPeer* dev = &app->scan_results[0];
                popup_set_text(app->popup, "Connecting...", 64, 36, AlignCenter, AlignCenter);
                if(!gap_connect(dev->address_type, dev->address)) {
                    popup_set_text(app->popup, "Connect failed", 64, 36, AlignCenter, AlignCenter);
                    scan_phase = ScanPhaseDone;
                }
            } else {
                // Multiple peers — show selection submenu
                scan_phase = ScanPhaseSelectPeer;
                submenu_reset(app->submenu);
                submenu_set_header(app->submenu, "Select Peer");
                static char labels[BC_MAX_PEERS][40];
                for(uint8_t i = 0; i < app->scan_result_count; i++) {
                    snprintf(labels[i], sizeof(labels[i]), "%s (%ddBm)",
                        app->scan_results[i].nickname, app->scan_results[i].rssi);
                    submenu_add_item(app->submenu, labels[i], 1000 + i, bc_peer_select_cb, app);
                }
                view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewSubmenu);
                furi_timer_stop(app->timer);
            }
        }
        return true;

    case ScanPhaseConnecting:
        if(gap_get_state() == GapStateConnected) {
            app->connected = true;
            app->connection_handle = gap_get_connection_handle_by_role(true);
            if(app->connection_handle == 0) {
                app->connection_handle = gap_get_connection_handle();
            }
            scan_phase = ScanPhaseDiscoverServices;
            popup_set_text(app->popup, "Connected!\nWaiting for MTU...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_exchange_mtu(app->connection_handle);
            app->tick_count = 0;
        } else if(app->tick_count >= CONNECT_TIMEOUT_POLLS) {
            popup_set_text(app->popup, "Connection\ntimeout", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
        }
        return true;

    case ScanPhaseDiscoverServices:
        if(app->tick_count == 5) {
            popup_set_text(app->popup, "Connected!\nDiscovering...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_services(app->connection_handle);
        }
        break;

    case ScanPhaseAnnounceDelay:
        if(app->tick_count >= 2) {
            BcAnnounce announce = {0};
            strncpy(announce.nickname, app->nickname, BC_MAX_NICKNAME);
            memcpy(announce.noise_pubkey, app->identity.noise_public, 32);
            announce.has_noise_key = true;
            memcpy(announce.ed25519_pubkey, app->identity.ed25519_public, 32);
            announce.has_ed25519_key = true;

            uint8_t pkt[BC_PAD_BLOCK_256];
            uint16_t pkt_len = bc_build_signed_announce_packet(
                pkt, sizeof(pkt), app->identity.peer_id, &announce,
                bc_sign_wrapper, &app->identity);
            if(pkt_len > 0) {
                FURI_LOG_I(TAG, "Sending signed announce (%d bytes)", pkt_len);
                ble_gatt_client_write(app->connection_handle, app->bc_char_handle, pkt, pkt_len);
                if(app->svc) {
                    ble_svc_bitchat_tx(app->svc, pkt, pkt_len);
                }
            }
            scene_manager_next_scene(app->scene_manager, BitchatSceneChat);
        }
        return true;

    default:
        break;
    }

check_gatt:
    if(event.type != SceneManagerEventTypeCustom) return false;

    // Handle peer selection from submenu
    if(event.event >= 1000 && event.event < (uint32_t)(1000 + app->scan_result_count)) {
        app->selected_scan_idx = event.event - 1000;
        BcPeer* dev = &app->scan_results[app->selected_scan_idx];

        scan_phase = ScanPhaseConnecting;
        app->tick_count = 0;
        popup_reset(app->popup);
        popup_set_header(app->popup, "BitChat", 64, 10, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Connecting...", 64, 36, AlignCenter, AlignCenter);
        view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewPopup);

        if(!gap_connect(dev->address_type, dev->address)) {
            popup_set_text(app->popup, "Connect failed", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
        }
        furi_timer_start(app->timer, 100);
        return true;
    }

    switch(scan_phase) {
    case ScanPhaseDiscoverServices:
        if(event.event == BitchatCustomEventServicesDiscovered) {
            int8_t svc_idx = -1;
            for(uint8_t i = 0; i < app->service_count; i++) {
                if(app->services[i].uuid_type == 2 &&
                   memcmp(app->services[i].uuid_128, bc_svc_uuid, 16) == 0) {
                    svc_idx = i;
                    break;
                }
            }
            if(svc_idx < 0) {
                popup_set_text(app->popup, "No BitChat service\non this device", 64, 36, AlignCenter, AlignCenter);
                scan_phase = ScanPhaseDone;
                return true;
            }
            scan_phase = ScanPhaseDiscoverChars;
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[svc_idx]);
            return true;
        }
        if(event.event == BitchatCustomEventGattError) {
            popup_set_text(app->popup, "Service discovery\nfailed", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
            return true;
        }
        break;

    case ScanPhaseDiscoverChars:
        if(event.event == BitchatCustomEventCharsDiscovered) {
            for(uint8_t i = 0; i < app->char_count; i++) {
                if(app->chars[i].uuid_type == 2 &&
                   memcmp(app->chars[i].uuid_128, bc_char_uuid, 16) == 0) {
                    app->bc_char_handle = app->chars[i].value_handle;
                    break;
                }
            }
            if(app->bc_char_handle == 0) {
                popup_set_text(app->popup, "BitChat characteristic\nnot found", 64, 36, AlignCenter, AlignCenter);
                scan_phase = ScanPhaseDone;
                return true;
            }
            scan_phase = ScanPhaseSubscribe;
            ble_gatt_client_subscribe_notifications(
                app->connection_handle, app->bc_char_handle, true);
            return true;
        }
        if(event.event == BitchatCustomEventGattError) {
            popup_set_text(app->popup, "Char discovery\nfailed", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
            return true;
        }
        break;

    case ScanPhaseSubscribe:
        if(event.event == BitchatCustomEventWriteComplete ||
           event.event == BitchatCustomEventGattError) {
            scan_phase = ScanPhaseAnnounceDelay;
            app->tick_count = 0;
            popup_set_text(app->popup, "Preparing\nannounce...", 64, 36, AlignCenter, AlignCenter);
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

void bitchat_scene_scan_on_exit(void* context) {
    BitchatApp* app = context;
    furi_timer_stop(app->timer);
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
    submenu_reset(app->submenu);
    popup_reset(app->popup);
}
