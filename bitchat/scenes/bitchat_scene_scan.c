#include "../bitchat_app_i.h"

#define SCAN_TIMEOUT_MS 10000
#define CONNECT_TIMEOUT_POLLS 100

// BitChat service UUID bytes for matching (little-endian)
static const uint8_t bc_svc_uuid[] = BITCHAT_SVC_UUID_128;
static const uint8_t bc_char_uuid[] = BITCHAT_CHAR_UUID_128;

typedef enum {
    ScanPhaseScanning,
    ScanPhaseConnecting,
    ScanPhaseDiscoverServices,
    ScanPhaseDiscoverChars,
    ScanPhaseSubscribe,
    ScanPhaseDone,
} ScanPhase;

static ScanPhase scan_phase;
static uint8_t target_addr[6];
static uint8_t target_addr_type;

// Scan callback — check for BitChat service UUID in advertisements
static void bc_scan_callback(GapScanResultData* result, void* context) {
    BitchatApp* app = context;
    if(!result->data || result->data_len == 0) return;

    // Look for 128-bit service UUID list (AD types 0x06 or 0x07) or
    // check for the service UUID in the advertisement
    uint8_t pos = 0;
    while(pos < result->data_len) {
        uint8_t len = result->data[pos];
        if(len == 0 || pos + len >= result->data_len) break;
        uint8_t type = result->data[pos + 1];

        // 128-bit UUID lists (incomplete=0x06, complete=0x07)
        if((type == 0x06 || type == 0x07) && len >= 17) {
            // Walk through 16-byte UUID entries
            for(uint8_t i = 0; i + 15 < len - 1; i += 16) {
                if(memcmp(&result->data[pos + 2 + i], bc_svc_uuid, 16) == 0) {
                    // Found a BitChat peer!
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    memcpy(target_addr, result->address, 6);
                    target_addr_type = result->address_type;

                    // Stop scanning and connect
                    gap_stop_scanning();
                    app->scanning = false;
                    furi_mutex_release(app->mutex);
                    return;
                }
            }
        }
        pos += len + 1;
    }
}

void bitchat_scene_scan_on_enter(void* context) {
    BitchatApp* app = context;

    scan_phase = ScanPhaseScanning;
    app->tick_count = 0;
    app->connected = false;
    app->bc_char_handle = 0;

    popup_reset(app->popup);
    popup_set_header(app->popup, "BitChat", 64, 10, AlignCenter, AlignTop);
    popup_set_text(app->popup, "Scanning for\nBitChat peers...", 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewPopup);

    // Disconnect existing
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    // Init GATT client for central-side operations
    extern void bitchat_gatt_callback(BleGattClientEvent* event, void* context);
    ble_gatt_client_init();
    ble_gatt_client_set_callback(bitchat_gatt_callback, app);

    gap_set_scan_callback(bc_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = SCAN_TIMEOUT_MS,
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
        if(!app->scanning) {
            // Scan callback found a peer and stopped scanning
            scan_phase = ScanPhaseConnecting;
            app->tick_count = 0;
            popup_set_text(app->popup, "Peer found!\nConnecting...", 64, 36, AlignCenter, AlignCenter);

            gap_set_scan_callback(NULL, NULL);
            if(!gap_connect(target_addr_type, target_addr)) {
                popup_set_text(app->popup, "Connect failed", 64, 36, AlignCenter, AlignCenter);
                scan_phase = ScanPhaseDone;
            }
        } else if(app->tick_count >= CONNECT_TIMEOUT_POLLS) {
            // Scan timeout — no peers found
            gap_stop_scanning();
            gap_set_scan_callback(NULL, NULL);
            app->scanning = false;
            popup_set_text(app->popup, "No BitChat peers\nfound nearby", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
        }
        return true;

    case ScanPhaseConnecting:
        if(gap_get_state() == GapStateConnected) {
            app->connected = true;
            app->connection_handle = gap_get_connection_handle();
            scan_phase = ScanPhaseDiscoverServices;
            popup_set_text(app->popup, "Connected!\nWaiting for MTU...", 64, 36, AlignCenter, AlignCenter);

            // Request MTU first, then wait before discovering services
            ble_gatt_client_exchange_mtu(app->connection_handle);
            // Delay service discovery to let MTU exchange complete
            app->tick_count = 0;
        } else if(app->tick_count >= CONNECT_TIMEOUT_POLLS) {
            popup_set_text(app->popup, "Connection\ntimeout", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
        }
        return true;

    case ScanPhaseDiscoverServices:
        // Wait ~500ms (5 ticks at 100ms) for MTU exchange before discovering
        if(app->tick_count == 5) {
            popup_set_text(app->popup, "Connected!\nDiscovering...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_services(app->connection_handle);
        }
        // Don't return true — let GATT events fall through to check_gatt
        break;

    default:
        break;
    }

check_gatt:
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(scan_phase) {
    case ScanPhaseDiscoverServices:
        if(event.event == BitchatCustomEventServicesDiscovered) {
            // Find BitChat service
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
            // Find BitChat characteristic
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
            // Subscribe to notifications
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
            // Subscribed (or failed, proceed anyway). Send our announce.
            BcAnnounce announce = {0};
            strncpy(announce.nickname, app->nickname, BC_MAX_NICKNAME);

            uint8_t pkt[128];
            uint16_t pkt_len = bc_build_announce_packet(pkt, sizeof(pkt), app->peer_id, &announce);
            if(pkt_len > 0) {
                FURI_LOG_I(TAG, "Sending announce (%d bytes) to handle 0x%04X",
                    pkt_len, app->bc_char_handle);
                ble_gatt_client_write(app->connection_handle, app->bc_char_handle, pkt, pkt_len);
            } else {
                FURI_LOG_E(TAG, "Failed to build announce packet");
            }

            // Go to chat scene
            scene_manager_next_scene(app->scene_manager, BitchatSceneChat);
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
    popup_reset(app->popup);
}
