#include "../bitchat_app_i.h"
#include "../crypto/ed25519_donna/ed25519.h"
#include <gui/modules/widget.h>

static void bc_chat_sign_wrapper(const uint8_t* data, uint16_t len, uint8_t* sig, void* ctx) {
    bc_identity_sign((const BcIdentity*)ctx, data, len, sig);
}

static uint32_t announce_tick_counter = 0;
#define ANNOUNCE_INTERVAL_TICKS 60 // Re-announce every 30 seconds (60 * 500ms) — less frequent to avoid GATT conflicts

static void send_announce_via_service(BitchatApp* app) {
    if(!app->svc) return;

    BcAnnounce announce = {0};
    strncpy(announce.nickname, app->nickname, BC_MAX_NICKNAME);
    memcpy(announce.noise_pubkey, app->identity.noise_public, 32);
    announce.has_noise_key = true;
    memcpy(announce.ed25519_pubkey, app->identity.ed25519_public, 32);
    announce.has_ed25519_key = true;

    uint8_t pkt[256];
    uint16_t pkt_len = bc_build_signed_announce_packet(
        pkt, sizeof(pkt), app->identity.peer_id, &announce,
        bc_chat_sign_wrapper, &app->identity);
    if(pkt_len > 0) {
        ble_svc_bitchat_tx(app->svc, pkt, pkt_len);
    }
}

static void process_incoming_packet(BitchatApp* app) {
    // Ignore empty notifications (keepalive/subscription confirmations)
    if(app->rx_len == 0) return;

    // Log raw bytes for debugging
    FURI_LOG_I(TAG, "RX %d bytes:", app->rx_len);
    for(uint16_t i = 0; i < app->rx_len && i < 32; i += 8) {
        uint16_t remain = app->rx_len - i;
        if(remain > 8) remain = 8;
        if(remain >= 8) {
            FURI_LOG_I(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X",
                app->rx_buf[i], app->rx_buf[i+1], app->rx_buf[i+2], app->rx_buf[i+3],
                app->rx_buf[i+4], app->rx_buf[i+5], app->rx_buf[i+6], app->rx_buf[i+7]);
        }
    }

    BcPacketHeader hdr;
    if(!bc_decode_header(app->rx_buf, app->rx_len, &hdr)) {
        FURI_LOG_W(TAG, "Failed to decode header (len=%d, first byte=0x%02X)",
            app->rx_len, app->rx_buf[0]);
        return;
    }

    FURI_LOG_I(TAG, "Packet: type=0x%02X ttl=%d payload=%d", hdr.type, hdr.ttl, hdr.payload_len);

    // Handle FRAGMENT packets — reassemble before processing
    if(hdr.type == BC_TYPE_FRAGMENT) {
        uint8_t reassembled[2048];
        uint16_t reassembled_len = bc_fragment_process(
            &app->frag_table, app->rx_buf, app->rx_len, reassembled, sizeof(reassembled));
        if(reassembled_len == 0) return; // incomplete, wait for more fragments

        // Replace rx_buf with reassembled packet and re-decode header
        if(reassembled_len <= sizeof(app->rx_buf)) {
            memcpy(app->rx_buf, reassembled, reassembled_len);
            app->rx_len = reassembled_len;
            if(!bc_decode_header(app->rx_buf, app->rx_len, &hdr)) return;
            FURI_LOG_I(TAG, "Reassembled: type=0x%02X payload=%d", hdr.type, hdr.payload_len);
        } else {
            return; // too large for our buffer
        }
    }

    // Deduplication — drop packets we've already processed
    uint16_t payload_offset = BC_HEADER_SIZE + BC_SENDER_ID_SIZE;
    if(hdr.has_recipient) payload_offset += BC_SENDER_ID_SIZE;
    if(payload_offset >= app->rx_len) return;

    if(bc_dedup_check(&app->dedup_table, &hdr,
                      &app->rx_buf[payload_offset], hdr.payload_len)) {
        FURI_LOG_D(TAG, "Duplicate packet — dropped");
        return;
    }

    // Relay: forward to other peers with decremented TTL
    uint8_t relay_ttl = bc_relay_should_forward(&hdr, app->identity.peer_id, app->peer_count);
    if(relay_ttl > 0) {
        // Create relay copy with decremented TTL
        uint8_t relay_buf[512];
        uint16_t relay_len = app->rx_len;
        if(relay_len <= sizeof(relay_buf)) {
            memcpy(relay_buf, app->rx_buf, relay_len);
            relay_buf[2] = relay_ttl; // update TTL

            // Send via peripheral notification (flood to all connected peers)
            if(app->svc) {
                ble_svc_bitchat_tx(app->svc, relay_buf, relay_len);
            }
            /* Snapshot central targets under the mutex, write without it.
             * Previously the BLE writes + furi_delay_ms ran under
             * app->mutex, which blocked bitchat_gatt_callback on the BLE
             * event thread. */
            struct {
                uint16_t handle;
                uint16_t char_handle;
            } relay_targets[BC_MAX_PEERS];
            uint8_t relay_target_count = 0;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(uint8_t i = 0; i < app->peer_count &&
                               relay_target_count < BC_MAX_PEERS; i++) {
                if(!app->peers[i].central_active ||
                   app->peers[i].central_char == 0) continue;
                relay_targets[relay_target_count].handle =
                    app->peers[i].central_handle;
                relay_targets[relay_target_count].char_handle =
                    app->peers[i].central_char;
                relay_target_count++;
            }
            furi_mutex_release(app->mutex);

            for(uint8_t i = 0; i < relay_target_count; i++) {
                if(i > 0) furi_delay_ms(50);
                ble_gatt_client_write(
                    relay_targets[i].handle,
                    relay_targets[i].char_handle,
                    relay_buf, relay_len);
            }
            FURI_LOG_D(TAG, "Relayed packet (new TTL=%d)", relay_ttl);
        }
    }

    const uint8_t* payload = &app->rx_buf[payload_offset];
    uint16_t payload_len = app->rx_len - payload_offset;
    if(payload_len > hdr.payload_len) payload_len = hdr.payload_len;

    // Verify Ed25519 signature if present
    if(hdr.flags & BC_FLAG_HAS_SIGNATURE) {
        uint16_t sig_offset = payload_offset + hdr.payload_len;
        if(sig_offset + BC_SIGNATURE_SIZE <= app->rx_len) {
            const uint8_t* signature = &app->rx_buf[sig_offset];
            const uint8_t* verify_key = NULL;

            if(hdr.type == BC_TYPE_ANNOUNCE) {
                // Self-authenticating: key is in the announce TLV
                BcAnnounce tmp;
                if(bc_decode_announce(payload, payload_len, &tmp) && tmp.has_ed25519_key) {
                    verify_key = tmp.ed25519_pubkey;
                }
            } else {
                // Look up signing key from peer table
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                for(uint8_t i = 0; i < app->peer_count; i++) {
                    if(memcmp(app->peers[i].peer_id, hdr.sender_id, BC_SENDER_ID_SIZE) == 0 &&
                       app->peers[i].has_signing_key) {
                        verify_key = app->peers[i].ed25519_pubkey;
                        break;
                    }
                }
                furi_mutex_release(app->mutex);
            }

            if(verify_key) {
                // Reconstruct signing data: packet without signature, ttl=0, flags=0, padded
                uint8_t verify_buf[BC_PAD_BLOCK_256];
                uint16_t verify_data_len = sig_offset;
                if(verify_data_len <= sizeof(verify_buf)) {
                    memcpy(verify_buf, app->rx_buf, verify_data_len);
                    verify_buf[2] = 0;  // ttl = 0
                    verify_buf[11] &= ~BC_FLAG_HAS_SIGNATURE; // clear only HAS_SIGNATURE bit
                    verify_data_len = bc_apply_padding(
                        verify_buf, verify_data_len, sizeof(verify_buf));

                    if(ed25519_sign_open(verify_buf, verify_data_len,
                                         verify_key, signature) != 0) {
                        FURI_LOG_W(TAG, "Signature INVALID — dropping packet");
                        return;
                    }
                    FURI_LOG_D(TAG, "Signature verified OK");
                }
            } else {
                FURI_LOG_D(TAG, "No verify key available, accepting unsigned");
            }
        }
    }

    switch(hdr.type) {
    case BC_TYPE_ANNOUNCE: {
        BcAnnounce announce;
        if(bc_decode_announce(payload, payload_len, &announce)) {
            FURI_LOG_I(TAG, "Announce from: %s", announce.nickname);
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool found = false;
            for(uint8_t i = 0; i < app->peer_count; i++) {
                if(memcmp(app->peers[i].peer_id, hdr.sender_id, BC_SENDER_ID_SIZE) == 0) {
                    strncpy(app->peers[i].nickname, announce.nickname, BC_MAX_NICKNAME);
                    app->peers[i].last_seen = furi_get_tick();
                    if(announce.has_ed25519_key) {
                        memcpy(app->peers[i].ed25519_pubkey, announce.ed25519_pubkey, 32);
                        app->peers[i].has_signing_key = true;
                    }
                    found = true;
                    break;
                }
            }
            if(!found && app->peer_count < BC_MAX_PEERS) {
                BcPeer* peer = &app->peers[app->peer_count];
                memcpy(peer->peer_id, hdr.sender_id, BC_SENDER_ID_SIZE);
                strncpy(peer->nickname, announce.nickname, BC_MAX_NICKNAME);
                if(announce.has_ed25519_key) {
                    memcpy(peer->ed25519_pubkey, announce.ed25519_pubkey, 32);
                    peer->has_signing_key = true;
                }
                peer->last_seen = furi_get_tick();
                peer->connected = true;
                app->peer_count++;

                char sys_msg[48];
                snprintf(sys_msg, sizeof(sys_msg), "%s joined", announce.nickname);
                bitchat_add_chat_message(app, "*", sys_msg);
            }
            furi_mutex_release(app->mutex);
        }
        break;
    }
    case BC_TYPE_MESSAGE: {
        // Android sends raw UTF-8 as the payload for broadcast messages
        if(payload_len > 0 && payload_len <= BC_MAX_MSG_CONTENT) {
            char content[BC_MAX_MSG_CONTENT + 1];
            memcpy(content, payload, payload_len);
            content[payload_len] = '\0';

            // Look up sender nickname from peer list
            const char* sender = "unknown";
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(uint8_t i = 0; i < app->peer_count; i++) {
                if(memcmp(app->peers[i].peer_id, hdr.sender_id, BC_SENDER_ID_SIZE) == 0) {
                    sender = app->peers[i].nickname;
                    break;
                }
            }
            FURI_LOG_I(TAG, "Message from %s: %s", sender, content);
            bitchat_add_chat_message(app, sender, content);
            furi_mutex_release(app->mutex);
            notification_message(app->notifications, &sequence_single_vibro);
        }
        break;
    }
    case BC_TYPE_LEAVE: {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        for(uint8_t i = 0; i < app->peer_count; i++) {
            if(memcmp(app->peers[i].peer_id, hdr.sender_id, BC_SENDER_ID_SIZE) == 0) {
                char sys_msg[48];
                snprintf(sys_msg, sizeof(sys_msg), "%s left", app->peers[i].nickname);
                bitchat_add_chat_message(app, "*", sys_msg);
                if(i < app->peer_count - 1) {
                    memmove(&app->peers[i], &app->peers[i + 1],
                            (app->peer_count - i - 1) * sizeof(BcPeer));
                }
                app->peer_count--;
                break;
            }
        }
        furi_mutex_release(app->mutex);
        break;
    }
    case BC_TYPE_NOISE_HS: {
        // Noise handshake message — find or create peer's handshake state
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        BcPeer* peer = NULL;
        for(uint8_t i = 0; i < app->peer_count; i++) {
            if(memcmp(app->peers[i].peer_id, hdr.sender_id, BC_SENDER_ID_SIZE) == 0) {
                peer = &app->peers[i];
                break;
            }
        }

        if(!peer) {
            FURI_LOG_W(TAG, "Noise handshake from unknown peer");
            furi_mutex_release(app->mutex);
            break;
        }

        if(!peer->noise_hs_active) {
            // Incoming handshake — we're the responder
            noise_handshake_init(&peer->noise_hs, NoiseRoleResponder,
                app->identity.noise_secret, app->identity.noise_public);
            peer->noise_hs_active = true;
        }

        // Read the handshake message
        if(!noise_handshake_read(&peer->noise_hs, payload, payload_len)) {
            FURI_LOG_E(TAG, "Noise handshake read failed");
            peer->noise_hs_active = false;
            furi_mutex_release(app->mutex);
            break;
        }

        // Write our response (if we have one)
        uint8_t hs_out[128];
        uint16_t hs_len = noise_handshake_write(&peer->noise_hs, hs_out, sizeof(hs_out));
        if(hs_len > 0) {
            // Send handshake response as type 0x10 packet
            uint8_t hs_pkt[256];
            uint16_t hs_hdr_len = bc_encode_header(
                hs_pkt, sizeof(hs_pkt), BC_TYPE_NOISE_HS, BC_DEFAULT_TTL,
                BC_FLAG_HAS_RECIPIENT, app->identity.peer_id, hs_out, hs_len);
            // Add recipient ID
            memcpy(&hs_pkt[hs_hdr_len], hdr.sender_id, BC_SENDER_ID_SIZE);
            memcpy(&hs_pkt[hs_hdr_len + BC_SENDER_ID_SIZE], hs_out, hs_len);
            uint16_t hs_total = hs_hdr_len + BC_SENDER_ID_SIZE + hs_len;

            if(app->svc) ble_svc_bitchat_tx(app->svc, hs_pkt, hs_total);
            if(peer->central_active && peer->central_char)
                ble_gatt_client_write(peer->central_handle, peer->central_char, hs_pkt, hs_total);
        }

        // Check if handshake is complete
        if(peer->noise_hs.complete) {
            noise_handshake_split(&peer->noise_hs, &peer->noise_session);
            peer->noise_hs_active = false;
            FURI_LOG_I(TAG, "Noise session established with %s", peer->nickname);

            char sys_msg[48];
            snprintf(sys_msg, sizeof(sys_msg), "%s: encrypted", peer->nickname);
            bitchat_add_chat_message(app, "*", sys_msg);
        }

        furi_mutex_release(app->mutex);
        break;
    }
    case BC_TYPE_NOISE_ENC: {
        // Encrypted message — find peer's session and decrypt
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        BcPeer* peer = NULL;
        for(uint8_t i = 0; i < app->peer_count; i++) {
            if(memcmp(app->peers[i].peer_id, hdr.sender_id, BC_SENDER_ID_SIZE) == 0) {
                peer = &app->peers[i];
                break;
            }
        }

        if(!peer || !peer->noise_session.active) {
            FURI_LOG_W(TAG, "Encrypted msg from peer without session");
            furi_mutex_release(app->mutex);
            break;
        }

        uint8_t decrypted[256];
        uint16_t dec_len = noise_session_decrypt(
            &peer->noise_session, payload, payload_len, decrypted, sizeof(decrypted));

        if(dec_len > 0) {
            // NoisePayload: [1B type][data]
            if(dec_len >= 2 && decrypted[0] == 0x01) {
                // PRIVATE_MESSAGE — extract content
                // Simple: just treat bytes 1..end as UTF-8 text
                char content[BC_MAX_MSG_CONTENT + 1];
                uint16_t content_len = dec_len - 1;
                if(content_len > BC_MAX_MSG_CONTENT) content_len = BC_MAX_MSG_CONTENT;
                memcpy(content, &decrypted[1], content_len);
                content[content_len] = '\0';

                FURI_LOG_I(TAG, "Encrypted msg from %s: %s", peer->nickname, content);
                bitchat_add_chat_message(app, peer->nickname, content);
                notification_message(app->notifications, &sequence_single_vibro);
            }
        } else {
            FURI_LOG_W(TAG, "Decryption failed");
        }

        furi_mutex_release(app->mutex);
        break;
    }
    default:
        FURI_LOG_D(TAG, "Unhandled packet type: 0x%02X", hdr.type);
        break;
    }
}

static void chat_msg_input_cb(void* context) {
    BitchatApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, BitchatCustomEventMsgSend);
}

static void chat_widget_button_cb(GuiButtonType result, InputType type, void* context) {
    if(type == InputTypeShort) {
        BitchatApp* app = context;
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

static void rebuild_chat_widget(BitchatApp* app) {
    widget_reset(app->widget);

    // Chat log as scrollable text (leave room for button)
    const char* log = furi_string_get_cstr(app->chat_log);
    if(furi_string_size(app->chat_log) > 0) {
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 49, log);
    } else {
        widget_add_string_element(
            app->widget, 64, 24, AlignCenter, AlignCenter, FontSecondary, "No messages yet");
    }

    // Send button (OK / center)
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Send",
        chat_widget_button_cb, app);
}

void bitchat_scene_chat_on_enter(void* context) {
    BitchatApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bitchat_add_chat_message(app, "*", "Connected to BitChat");
    furi_mutex_release(app->mutex);

    rebuild_chat_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewWidget);

    furi_timer_start(app->timer, 500);
}

bool bitchat_scene_chat_on_event(void* context, SceneManagerEvent event) {
    BitchatApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BitchatCustomEventTick) {
            // Periodic re-announce via peripheral service
            announce_tick_counter++;
            if(announce_tick_counter >= ANNOUNCE_INTERVAL_TICKS) {
                announce_tick_counter = 0;
                send_announce_via_service(app);
            }
            // Don't rebuild on tick — only on new content
            return true;
        } else if(event.event == BitchatCustomEventNotification) {
            process_incoming_packet(app);
            rebuild_chat_widget(app);
            return true;
        } else if(event.event == BitchatCustomEventMsgSend) {
            /* User submitted a message.
             *
             * The previous version held app->mutex for the entire send
             * loop — across `noise_session_encrypt`, `furi_delay_ms`,
             * and `ble_gatt_client_write`. If any BLE notification
             * arrived mid-send, bitchat_gatt_callback on the BLE event
             * thread blocked on the mutex and the UI froze. Restructure:
             *
             *   1. Skip empty input.
             *   2. Build the broadcast packet (no shared state — safe
             *      outside the mutex).
             *   3. Snapshot active central peers into a small local
             *      array under the mutex, then release it.
             *   4. Do all crypto + BLE writes with the mutex released.
             *      Noise session counters are per-peer and only written
             *      here (chat-scene view thread), so that's safe.
             *   5. Update the local chat log under a brief mutex
             *      section again at the end.
             *
             * Also: drop the redundant "legacy single connection" write
             * path — for any peer we're central to, it's already in the
             * peer table, so that branch duplicated every write and put
             * two ATT requests in flight on the same link (hitting
             * HCI_COMMAND_DISALLOWED 0x0C). */
            if(app->input_buf[0] == '\0') return true;

            uint8_t pkt[BC_PAD_BLOCK_256];
            uint16_t pkt_len = bc_build_signed_broadcast_packet(
                pkt, sizeof(pkt), app->identity.peer_id, app->input_buf,
                bc_chat_sign_wrapper, &app->identity);
            if(pkt_len == 0) {
                FURI_LOG_W(TAG, "Failed to build broadcast packet");
                app->input_buf[0] = '\0';
                return true;
            }
            FURI_LOG_I(TAG, "Sending message: %s (%d bytes)",
                app->input_buf, pkt_len);

            /* Snapshot peer targets under a short-lived mutex section. */
            struct {
                uint16_t handle;
                uint16_t char_handle;
                bool have_session;
                uint8_t recipient_id[BC_SENDER_ID_SIZE];
                BcPeer* peer_ptr;  /* for noise_session_encrypt — peer slots
                                    * don't move or get freed while a
                                    * central_active connection is open */
                char nickname[BC_MAX_NICKNAME + 1];
            } targets[BC_MAX_PEERS];
            uint8_t target_count = 0;

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(uint8_t i = 0; i < app->peer_count && target_count < BC_MAX_PEERS; i++) {
                BcPeer* peer = &app->peers[i];
                if(!peer->central_active || peer->central_char == 0) continue;
                targets[target_count].handle = peer->central_handle;
                targets[target_count].char_handle = peer->central_char;
                targets[target_count].have_session = peer->noise_session.active;
                memcpy(targets[target_count].recipient_id, peer->peer_id,
                       BC_SENDER_ID_SIZE);
                strncpy(targets[target_count].nickname, peer->nickname,
                        BC_MAX_NICKNAME);
                targets[target_count].nickname[BC_MAX_NICKNAME] = '\0';
                targets[target_count].peer_ptr = peer;
                target_count++;
            }
            furi_mutex_release(app->mutex);

            /* Writes + crypto run WITHOUT the mutex held.
             * Space writes ~50ms apart so consecutive ATT requests don't
             * collide on the same link (upstream fix commit 14b8b2bf
             * used a similar delay). A proper state-machine wait on
             * BleGattClientEventWriteComplete would be cleaner but isn't
             * needed for the freeze fix. */
            for(uint8_t i = 0; i < target_count; i++) {
                if(targets[i].have_session) {
                    uint8_t payload_buf[256];
                    uint16_t content_len = strlen(app->input_buf);
                    if(content_len > 200) content_len = 200;
                    payload_buf[0] = 0x01; /* PRIVATE_MESSAGE */
                    memcpy(&payload_buf[1], app->input_buf, content_len);

                    uint8_t enc_buf[300];
                    uint16_t enc_len = noise_session_encrypt(
                        &targets[i].peer_ptr->noise_session,
                        payload_buf, 1 + content_len,
                        enc_buf, sizeof(enc_buf));
                    if(enc_len == 0) continue;

                    uint8_t enc_pkt[512];
                    uint16_t hdr_len = bc_encode_header(
                        enc_pkt, sizeof(enc_pkt),
                        BC_TYPE_NOISE_ENC, BC_DEFAULT_TTL,
                        BC_FLAG_HAS_RECIPIENT,
                        app->identity.peer_id, enc_buf, enc_len);
                    memcpy(&enc_pkt[hdr_len], targets[i].recipient_id,
                           BC_SENDER_ID_SIZE);
                    memcpy(&enc_pkt[hdr_len + BC_SENDER_ID_SIZE],
                           enc_buf, enc_len);
                    uint16_t total = hdr_len + BC_SENDER_ID_SIZE + enc_len;

                    if(i > 0) furi_delay_ms(50);
                    ble_gatt_client_write(
                        targets[i].handle, targets[i].char_handle,
                        enc_pkt, total);
                    FURI_LOG_I(TAG, "Sent encrypted msg to %s (%d bytes)",
                        targets[i].nickname, total);
                } else {
                    if(i > 0) furi_delay_ms(50);
                    ble_gatt_client_write(
                        targets[i].handle, targets[i].char_handle,
                        pkt, pkt_len);
                    FURI_LOG_I(TAG, "Sent plain msg to %s (%d bytes)",
                        targets[i].nickname, pkt_len);
                }
            }

            /* Notify any peers connected to us via our peripheral service.
             * No mutex; ble_svc_bitchat_tx is internally serialized. */
            if(app->svc) {
                ble_svc_bitchat_tx(app->svc, pkt, pkt_len);
            }

            announce_tick_counter = 0;

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bitchat_add_chat_message(app, app->nickname, app->input_buf);
            furi_mutex_release(app->mutex);
            app->input_buf[0] = '\0';
            // Return to chat widget
            rebuild_chat_widget(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewWidget);
            return true;
        } else if(event.event == GuiButtonTypeCenter) {
            // Send button pressed — open text input
            app->input_buf[0] = '\0';
            text_input_reset(app->text_input);
            text_input_set_header_text(app->text_input, "Send Message");
            text_input_set_result_callback(
                app->text_input, chat_msg_input_cb, app,
                app->input_buf, BC_MAX_MSG_CONTENT, true);
            view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewTextInput);
            return true;
        } else if(event.event == BitchatCustomEventGattError) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bitchat_add_chat_message(app, "*", "Connection lost");
            furi_mutex_release(app->mutex);
            rebuild_chat_widget(app);
            app->connected = false;
            return true;
        } else if(event.event == BitchatCustomEventWriteComplete) {
            FURI_LOG_I(TAG, "GATT write complete");
            return true;
        }
    }

    return false;
}

void bitchat_scene_chat_on_exit(void* context) {
    BitchatApp* app = context;
    furi_timer_stop(app->timer);

    // Don't disconnect here — app_free handles BLE cleanup via bt_disconnect
    widget_reset(app->widget);
    text_box_reset(app->text_box);
}
