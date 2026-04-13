#include "../ecovacs_app_i.h"

static const uint8_t ecovacs_aes_key[16] = ECOVACS_AES_KEY;

// Benign probe: request device info (read-only, non-destructive)
static const char probe_json[] =
    "{\"header\":{\"pri\":1,\"ts\":\"0\",\"ver\":\"0.0.1\"},"
    "\"body\":{\"data\":{\"act\":\"getInfo\"}}}";

static void append_log(EcovacsAuditorApp* app, const char* text) {
    furi_string_cat_str(app->audit_log, text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
}

static void append_hex(EcovacsAuditorApp* app, const uint8_t* data, uint16_t len) {
    uint16_t show = len > 20 ? 20 : len;
    for(uint16_t i = 0; i < show; i++) {
        furi_string_cat_printf(app->audit_log, "%02X ", data[i]);
    }
    if(len > 20) furi_string_cat_str(app->audit_log, "...");
    furi_string_cat_str(app->audit_log, "\n");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
}

static void advance_phase(EcovacsAuditorApp* app) {
    switch(app->audit_phase) {
    case EcoAuditSubscribe:
        app->audit_phase = EcoAuditProbeUnencrypted;
        append_log(app, "[2] Unencrypted probe...\n");
        // Write a simple ASCII probe to see if device responds to raw data
        {
            const uint8_t ping[] = "getinfo";
            ble_gatt_client_write(
                app->connection_handle, app->cmd_handle,
                ping, sizeof(ping) - 1);
        }
        break;

    case EcoAuditProbeUnencrypted:
        app->audit_phase = EcoAuditProbeEncrypted;
        append_log(app, "[3] Encrypted probe...\n");
        append_log(app, "  Key: 12345678ecovacs\n");
        // Encrypt the JSON probe with the static AES key
        {
            uint8_t encrypted[ECOVACS_MAX_PAYLOAD];
            uint16_t enc_len = aes128_ecb_encrypt_padded(
                ecovacs_aes_key,
                (const uint8_t*)probe_json,
                strlen(probe_json),
                encrypted);

            furi_string_cat_printf(app->audit_log, "  Sending %d bytes...\n", enc_len);
            text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));

            ble_gatt_client_write(
                app->connection_handle, app->cmd_handle,
                encrypted, enc_len);
        }
        break;

    case EcoAuditProbeEncrypted:
        app->audit_phase = EcoAuditDone;
        append_log(app, "\n--- Audit Complete ---\n\n");

        // Always vulnerable if we got this far (connected without auth + service exposed)
        app->svc_vulnerable = true;
        append_log(app, "VULNERABLE:\n");
        append_log(app, "  No BLE authentication\n");
        append_log(app, "  Service 0x8888 exposed\n");
        append_log(app, "  Chars FF01/FF02 present\n");

        if(app->key_vulnerable) {
            append_log(app, "  Static AES key WORKS\n");
            append_log(app, "\nCVE-2024-12078 confirmed.\n");
            append_log(app, "Full device control is\npossible over BLE.\n");
        } else {
            append_log(app, "\n  AES key response:\n  inconclusive\n");
            append_log(app, "  (key may be different\n  for this model, or\n  device may be patched)\n");
        }

        notification_message(app->notifications, &sequence_success);
        break;

    case EcoAuditDone:
        break;
    }
}

static void handle_response(EcovacsAuditorApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint8_t resp[ECOVACS_MAX_PAYLOAD];
    uint16_t resp_len = app->resp_len;
    memcpy(resp, app->resp_buf, resp_len);
    app->has_response = false;
    furi_mutex_release(app->mutex);

    furi_string_cat_printf(app->audit_log, "  Got %d bytes: ", resp_len);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
    append_hex(app, resp, resp_len);

    if(app->audit_phase == EcoAuditProbeEncrypted && resp_len >= 16) {
        // Try to decrypt response with the static key
        uint8_t decrypted[ECOVACS_MAX_PAYLOAD];
        uint16_t dec_len = aes128_ecb_decrypt_unpadded(
            ecovacs_aes_key, resp, resp_len, decrypted);

        if(dec_len > 0) {
            // Check if decrypted data looks like JSON or valid ASCII
            bool looks_valid = false;
            for(uint16_t i = 0; i < dec_len && i < 32; i++) {
                if(decrypted[i] == '{' || decrypted[i] == '"') {
                    looks_valid = true;
                    break;
                }
            }
            if(looks_valid) {
                app->key_vulnerable = true;
                append_log(app, "  Decrypted (JSON!):\n  ");
                uint16_t show = dec_len > 60 ? 60 : dec_len;
                // Safe string display
                for(uint16_t i = 0; i < show; i++) {
                    char c = (char)decrypted[i];
                    if(c >= 0x20 && c < 0x7F) {
                        furi_string_push_back(app->audit_log, c);
                    } else {
                        furi_string_cat_str(app->audit_log, ".");
                    }
                }
                furi_string_cat_str(app->audit_log, "\n");
                text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
            }
        }
    }

    advance_phase(app);
}

void ecovacs_scene_audit_on_enter(void* context) {
    EcovacsAuditorApp* app = context;

    furi_string_reset(app->audit_log);
    app->audit_phase = EcoAuditSubscribe;
    app->svc_vulnerable = false;
    app->key_vulnerable = false;
    app->has_response = false;

    furi_string_cat_printf(app->audit_log,
        "=== Ecovacs Audit ===\nDevice: %s\n\n",
        app->current_device.has_name ? app->current_device.name : "Unknown");

    append_log(app, "[1] Subscribe to 0xFF01...\n");

    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
    text_box_set_font(app->text_box, TextBoxFontText);
    view_dispatcher_switch_to_view(app->view_dispatcher, EcovacsViewTextBox);

    // Subscribe to response notifications
    ble_gatt_client_subscribe_notifications(app->connection_handle, app->rsp_handle, true);

    // Brief delay for subscription to settle, then start probes
    furi_timer_start(app->timer, 500);
}

bool ecovacs_scene_audit_on_event(void* context, SceneManagerEvent event) {
    EcovacsAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == EcovacsCustomEventTick) {
        furi_timer_stop(app->timer);
        if(app->audit_phase == EcoAuditSubscribe) {
            advance_phase(app);
        } else if(app->audit_phase != EcoAuditDone) {
            // Timeout waiting for response — move on
            append_log(app, "  (no response)\n");
            advance_phase(app);
        }
        return true;
    }

    if(event.event == EcovacsCustomEventWriteComplete) {
        // Command sent — wait for notification response
        furi_timer_start(app->timer, 3000);
        return true;
    }

    if(event.event == EcovacsCustomEventNotification) {
        furi_timer_stop(app->timer);
        handle_response(app);
        return true;
    }

    if(event.event == EcovacsCustomEventGattError) {
        append_log(app, "  [!] GATT error\n");
        advance_phase(app);
        return true;
    }

    return false;
}

void ecovacs_scene_audit_on_exit(void* context) {
    EcovacsAuditorApp* app = context;
    furi_timer_stop(app->timer);

    if(app->connected && app->rsp_handle) {
        ble_gatt_client_subscribe_notifications(app->connection_handle, app->rsp_handle, false);
    }
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }

    text_box_reset(app->text_box);
}
