#include "../bitchat_app_i.h"
#include <gui/modules/widget.h>

static void bc_chat_sign_wrapper(const uint8_t* data, uint16_t len, uint8_t* sig, void* ctx) {
    bc_identity_sign((const BcIdentity*)ctx, data, len, sig);
}

static uint32_t announce_tick_counter = 0;
#define ANNOUNCE_INTERVAL_TICKS 8 // Re-announce every 4 seconds (8 * 500ms)

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

    uint16_t payload_offset = BC_HEADER_SIZE + BC_SENDER_ID_SIZE;
    if(hdr.has_recipient) payload_offset += BC_SENDER_ID_SIZE;
    if(payload_offset >= app->rx_len) return;

    const uint8_t* payload = &app->rx_buf[payload_offset];
    uint16_t payload_len = app->rx_len - payload_offset;
    if(payload_len > hdr.payload_len) payload_len = hdr.payload_len;

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
                    found = true;
                    break;
                }
            }
            if(!found && app->peer_count < BC_MAX_PEERS) {
                BcPeer* peer = &app->peers[app->peer_count];
                memcpy(peer->peer_id, hdr.sender_id, BC_SENDER_ID_SIZE);
                strncpy(peer->nickname, announce.nickname, BC_MAX_NICKNAME);
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
        BcMessage msg;
        if(bc_decode_message(payload, payload_len, &msg)) {
            FURI_LOG_I(TAG, "Message from %s: %s", msg.sender, msg.content);
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bitchat_add_chat_message(app, msg.sender, msg.content);
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
            // Periodic re-announce via peripheral service (every 4s)
            announce_tick_counter++;
            if(announce_tick_counter >= ANNOUNCE_INTERVAL_TICKS) {
                announce_tick_counter = 0;
                send_announce_via_service(app);
            }
            // Refresh chat display
            rebuild_chat_widget(app);
            return true;
        } else if(event.event == BitchatCustomEventNotification) {
            process_incoming_packet(app);
            rebuild_chat_widget(app);
            return true;
        } else if(event.event == BitchatCustomEventMsgSend) {
            // User submitted a message — send as raw UTF-8 broadcast
            if(app->input_buf[0] != '\0' && app->connected) {
                uint8_t pkt[BC_PAD_BLOCK_256];
                uint16_t pkt_len = bc_build_signed_broadcast_packet(
                    pkt, sizeof(pkt), app->identity.peer_id, app->input_buf,
                    bc_chat_sign_wrapper, &app->identity);

                if(pkt_len > 0) {
                    FURI_LOG_I(TAG, "Sending message: %s (%d bytes)", app->input_buf, pkt_len);
                    ble_gatt_client_write(
                        app->connection_handle, app->bc_char_handle, pkt, pkt_len);
                }

                furi_mutex_acquire(app->mutex, FuriWaitForever);
                bitchat_add_chat_message(app, app->nickname, app->input_buf);
                furi_mutex_release(app->mutex);
                app->input_buf[0] = '\0';
            }
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
