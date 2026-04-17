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
    ScanPhaseAnnounceDelay,   /* tick-driven: wait 2 ticks, send announce */
    ScanPhaseWaitAnnounceAck, /* event-driven: on WriteComplete, send msg1 */
    ScanPhaseWaitMsg1Ack,     /* event-driven: on WriteComplete, goto chat */
    ScanPhaseDone,
} ScanPhase;

static ScanPhase scan_phase;

/* Noise handshake msg1 is built at announce-send time and held here until
 * the announce write's ACK arrives, so we never have two ATT writes in
 * flight on the same link (ATT permits only one outstanding request; the
 * second returns HCI_COMMAND_DISALLOWED = 0x0C). */
static uint8_t pending_hs_pkt[128];
static uint16_t pending_hs_pkt_len;

// Dual-role tie-breaking: compare MAC addresses.
// Lower MAC acts as central (initiates connection).
// Returns true if we should connect as central to this peer.
static bool should_we_be_central(const uint8_t our_mac[6], const uint8_t peer_mac[6]) {
    return memcmp(our_mac, peer_mac, 6) < 0;
}

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

    /* Ignore any advertisement that claims our own MAC. The STM32WB radio
     * can occasionally observe its own adverts, and even without that, a
     * nearby repeater could echo us back. */
    if(memcmp(result->address, app->our_mac, 6) == 0) return;

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

    /* Parse advertised nickname up front so we can dedupe by identity
     * rather than by MAC. Android and iOS rotate their BLE address
     * (Resolvable Private Address) roughly every 15 minutes, so a single
     * phone can appear as 2-3 distinct MACs during a 10 s scan if we
     * only key on address. The nickname is stable across RPA rotations
     * for the same logical peer. */
    char adv_name[32] = {0};
    bool have_name = parse_adv_name(result->data, result->data_len, adv_name, sizeof(adv_name));

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    /* Dedupe pass: prefer a nickname match over a MAC match. Update the
     * existing entry's current MAC and RSSI so the user can still connect
     * to the latest RPA after rotation. */
    for(uint8_t i = 0; i < app->scan_result_count; i++) {
        BcPeer* existing = &app->scan_results[i];
        bool mac_match = (memcmp(existing->address, result->address, 6) == 0);
        bool name_match = have_name && existing->nickname[0] != '\0' &&
                          strncmp(existing->nickname, adv_name, BC_MAX_NICKNAME) == 0;
        if(mac_match || name_match) {
            if(!mac_match) {
                /* Same nickname, new RPA — refresh the address. */
                memcpy(existing->address, result->address, 6);
                existing->address_type = result->address_type;
            }
            existing->rssi = result->rssi;
            furi_mutex_release(app->mutex);
            return;
        }
    }

    if(app->scan_result_count < BC_MAX_PEERS) {
        BcPeer* dev = &app->scan_results[app->scan_result_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        if(have_name) {
            strncpy(dev->nickname, adv_name, BC_MAX_NICKNAME);
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

    ble_gatt_client_init();
    /* Per-connection callback registered once we know the handle (ScanPhaseConnecting). */

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
                // Single peer — check tie-breaking before connecting
                BcPeer* dev = &app->scan_results[0];
                if(should_we_be_central(app->our_mac, dev->address)) {
                    app->selected_scan_idx = 0;
                    scan_phase = ScanPhaseConnecting;
                    app->tick_count = 0;
                    popup_set_text(app->popup, "Connecting...", 64, 36, AlignCenter, AlignCenter);
                    if(!gap_connect(dev->address_type, dev->address)) {
                        popup_set_text(app->popup, "Connect failed", 64, 36, AlignCenter, AlignCenter);
                        scan_phase = ScanPhaseDone;
                    }
                } else {
                    // Higher MAC — wait for peer to connect to us
                    popup_set_text(app->popup, "Peer found!\nWaiting for them\nto connect to us...", 64, 36, AlignCenter, AlignCenter);
                    scan_phase = ScanPhaseDone;
                    // The peer's scan will find our advertisement and connect as central
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

    case ScanPhaseConnecting: {
        /* Dual-role hazard: if a peer is already connected to us as peripheral,
         * gap_get_state() returns Connected the moment we enter this phase,
         * *before* our outbound central connect completes. The legacy
         * gap_get_connection_handle() fallback would then return the peer's
         * handle — and we'd run discovery/writes against the wrong link.
         * Require an actual central-role slot specifically. */
        uint16_t central_handle = gap_get_connection_handle_by_role(true);
        if(central_handle != 0) {
            app->connected = true;
            app->connection_handle = central_handle;
            extern void bitchat_gatt_callback(BleGattClientEvent* event, void* context);
            ble_gatt_client_set_callback(app->connection_handle, bitchat_gatt_callback, app);
            scan_phase = ScanPhaseDiscoverServices;
            popup_set_text(app->popup, "Connected!\nWaiting for MTU...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_exchange_mtu(app->connection_handle);
            app->tick_count = 0;
        } else if(app->tick_count >= CONNECT_TIMEOUT_POLLS) {
            popup_set_text(app->popup, "Connection\ntimeout", 64, 36, AlignCenter, AlignCenter);
            scan_phase = ScanPhaseDone;
        }
    }
        return true;

    case ScanPhaseDiscoverServices:
        if(app->tick_count == 5) {
            popup_set_text(app->popup, "Connected!\nDiscovering...", 64, 36, AlignCenter, AlignCenter);
            ble_gatt_client_discover_services(app->connection_handle);
        }
        break;

    case ScanPhaseAnnounceDelay: {
        if(app->tick_count < 2) return true;

        /* Step 1: claim / update peer slot — brief mutex section only. */
        int8_t peer_idx = -1;
        if(app->selected_scan_idx < app->scan_result_count) {
            BcPeer* scan_dev = &app->scan_results[app->selected_scan_idx];
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(uint8_t i = 0; i < app->peer_count; i++) {
                if(memcmp(app->peers[i].address, scan_dev->address, 6) == 0) {
                    peer_idx = i;
                    break;
                }
            }
            if(peer_idx < 0 && app->peer_count < BC_MAX_PEERS) {
                peer_idx = app->peer_count++;
                memcpy(app->peers[peer_idx].address, scan_dev->address, 6);
                strncpy(app->peers[peer_idx].nickname, scan_dev->nickname, BC_MAX_NICKNAME);
            }
            if(peer_idx >= 0) {
                app->peers[peer_idx].central_handle = app->connection_handle;
                app->peers[peer_idx].central_char = app->bc_char_handle;
                app->peers[peer_idx].central_active = true;
                app->peers[peer_idx].connected = true;
                app->peers[peer_idx].last_seen = furi_get_tick();
            }
            furi_mutex_release(app->mutex);
        }

        /* Step 2: build announce + pre-build handshake msg1 OUTSIDE the mutex.
         * Crypto (Ed25519 signing, Noise X25519) takes milliseconds and used
         * to run with app->mutex held, which blocked bitchat_gatt_callback
         * on the BLE event thread if a notification arrived mid-handshake.
         * Identity + scene-local peer ownership means this is safe without
         * the mutex. */
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

        pending_hs_pkt_len = 0;
        if(peer_idx >= 0) {
            BcPeer* peer = &app->peers[peer_idx];
            noise_handshake_init(&peer->noise_hs, NoiseRoleInitiator,
                app->identity.noise_secret, app->identity.noise_public);
            peer->noise_hs_active = true;

            uint8_t hs_out[64];
            uint16_t hs_len = noise_handshake_write(
                &peer->noise_hs, hs_out, sizeof(hs_out));
            if(hs_len > 0) {
                uint16_t hs_hdr = bc_encode_header(
                    pending_hs_pkt, sizeof(pending_hs_pkt), BC_TYPE_NOISE_HS,
                    BC_DEFAULT_TTL, 0, app->identity.peer_id, hs_out, hs_len);
                memcpy(&pending_hs_pkt[hs_hdr], hs_out, hs_len);
                pending_hs_pkt_len = hs_hdr + hs_len;
            }
        }

        /* Step 3: fire announce write. Wait for the ACK before queuing msg1
         * — ATT permits only one outstanding write request per link. */
        if(pkt_len > 0) {
            FURI_LOG_I(TAG, "Sending signed announce (%d bytes)", pkt_len);
            if(app->svc) {
                ble_svc_bitchat_tx(app->svc, pkt, pkt_len);
            }
            bool ok = ble_gatt_client_write(
                app->connection_handle, app->bc_char_handle, pkt, pkt_len);
            if(ok) {
                scan_phase = ScanPhaseWaitAnnounceAck;
            } else {
                /* Submit failed — can't recover, skip ahead. */
                scan_phase = ScanPhaseDone;
                scene_manager_next_scene(app->scene_manager, BitchatSceneChat);
            }
        } else {
            /* No announce to send — skip directly to handshake. */
            scan_phase = ScanPhaseWaitAnnounceAck;
            /* Synthesize WriteComplete so the next phase fires on the next event. */
            view_dispatcher_send_custom_event(
                app->view_dispatcher, BitchatCustomEventWriteComplete);
        }
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
        FURI_LOG_I(TAG,
            "Peer selected idx=%u name=%s addr=%02X:%02X:%02X:%02X:%02X:%02X type=%u",
            app->selected_scan_idx, dev->nickname,
            dev->address[5], dev->address[4], dev->address[3],
            dev->address[2], dev->address[1], dev->address[0],
            dev->address_type);

        scan_phase = ScanPhaseConnecting;
        app->tick_count = 0;
        popup_reset(app->popup);
        popup_set_header(app->popup, "BitChat", 64, 10, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Connecting...", 64, 36, AlignCenter, AlignCenter);
        view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewPopup);

        if(!gap_connect(dev->address_type, dev->address)) {
            FURI_LOG_E(TAG, "gap_connect returned false");
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

    case ScanPhaseWaitAnnounceAck:
        if(event.event == BitchatCustomEventWriteComplete ||
           event.event == BitchatCustomEventGattError) {
            /* Announce acked (or errored — either way the ATT slot is now
             * free). Fire the pre-built Noise handshake msg1. */
            if(pending_hs_pkt_len > 0) {
                FURI_LOG_I(TAG, "Sending Noise handshake msg1 (%d bytes)",
                    pending_hs_pkt_len);
                bool ok = ble_gatt_client_write(
                    app->connection_handle, app->bc_char_handle,
                    pending_hs_pkt, pending_hs_pkt_len);
                if(ok) {
                    scan_phase = ScanPhaseWaitMsg1Ack;
                } else {
                    scan_phase = ScanPhaseDone;
                    scene_manager_next_scene(app->scene_manager, BitchatSceneChat);
                }
            } else {
                scene_manager_next_scene(app->scene_manager, BitchatSceneChat);
            }
            return true;
        }
        break;

    case ScanPhaseWaitMsg1Ack:
        if(event.event == BitchatCustomEventWriteComplete ||
           event.event == BitchatCustomEventGattError) {
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
    submenu_reset(app->submenu);
    popup_reset(app->popup);
}
