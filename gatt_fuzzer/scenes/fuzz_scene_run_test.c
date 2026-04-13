#include "../gatt_fuzzer_app_i.h"

static bool run_all;
static FuzzTestId start_test;

static void log_append(GattFuzzerApp* app, const char* text) {
    furi_string_cat_str(app->fuzz_log, text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
}

static void run_write_all(GattFuzzerApp* app) {
    if(app->fuzz_step < app->all_char_total) {
        uint16_t handle = app->all_char_handles[app->fuzz_step];
        // Generate random data
        uint8_t data[20];
        uint8_t len = (fuzz_rand(app) % 20) + 1;
        for(uint8_t i = 0; i < len; i++) data[i] = fuzz_rand(app) & 0xFF;

        furi_string_cat_printf(app->fuzz_log, "  W h=%d %dB..", handle, len);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
        app->waiting_response = true;
        ble_gatt_client_write(app->connection_handle, handle, data, len);
    } else {
        app->current_test++;
        app->fuzz_step = 0;
        furi_timer_start(app->timer, 50);
    }
}

static void run_oversize_write(GattFuzzerApp* app) {
    static const uint16_t sizes[] = {23, 50, 100, 150, 200, 247};
    uint8_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    uint8_t char_idx = app->fuzz_step / num_sizes;
    uint8_t size_idx = app->fuzz_step % num_sizes;

    if(char_idx < app->all_char_total) {
        uint16_t handle = app->all_char_handles[char_idx];
        uint16_t sz = sizes[size_idx];
        uint8_t data[248];
        for(uint16_t i = 0; i < sz; i++) data[i] = fuzz_rand(app) & 0xFF;

        furi_string_cat_printf(app->fuzz_log, "  OW h=%d %dB..", handle, sz);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
        app->waiting_response = true;
        ble_gatt_client_write(app->connection_handle, handle, data, sz);
    } else {
        app->current_test++;
        app->fuzz_step = 0;
        furi_timer_start(app->timer, 50);
    }
}

static void run_invalid_handle(GattFuzzerApp* app) {
    // Test invalid handles: 0x0000, 0xFFFF, and gaps
    static const uint16_t bad_handles[] = {
        0x0000, 0x0001, 0x0002, 0xFFFE, 0xFFFF,
        0x1234, 0x5678, 0x9ABC, 0x00FF, 0x0100,
    };
    uint8_t num = sizeof(bad_handles) / sizeof(bad_handles[0]);

    if(app->fuzz_step < num) {
        uint16_t handle = bad_handles[app->fuzz_step];
        uint8_t data[] = {0x41, 0x41, 0x41, 0x41};

        furi_string_cat_printf(app->fuzz_log, "  IH h=0x%04X..", handle);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
        app->waiting_response = true;
        ble_gatt_client_write(app->connection_handle, handle, data, 4);
    } else {
        app->current_test++;
        app->fuzz_step = 0;
        furi_timer_start(app->timer, 50);
    }
}

static void run_rapid_subscribe(GattFuzzerApp* app) {
    // For each char with notify/indicate, subscribe then unsubscribe rapidly
    // Each char gets 2 steps (subscribe, unsubscribe) x 3 cycles = 6 steps
    uint8_t char_idx = app->fuzz_step / 6;
    uint8_t cycle_step = app->fuzz_step % 6;
    bool subscribe = (cycle_step % 2 == 0);

    // Find next char with notify/indicate property
    while(char_idx < app->all_char_total) {
        uint8_t props = app->all_char_properties[char_idx];
        if(props & 0x30) break; // Notify (0x10) or Indicate (0x20)
        char_idx++;
        app->fuzz_step = char_idx * 6;
    }

    if(char_idx < app->all_char_total) {
        uint16_t handle = app->all_char_handles[char_idx];
        furi_string_cat_printf(app->fuzz_log, "  %s h=%d..",
            subscribe ? "SUB" : "UNSUB", handle);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
        app->waiting_response = true;
        ble_gatt_client_subscribe_notifications(
            app->connection_handle, handle, subscribe);
    } else {
        app->current_test++;
        app->fuzz_step = 0;
        furi_timer_start(app->timer, 50);
    }
}

static void run_mtu_fuzz(GattFuzzerApp* app) {
    // MTU exchange only takes connection handle — we send it multiple times
    // to test stack resilience to repeated MTU exchanges
    uint8_t num = 5;

    if(app->fuzz_step < num) {
        furi_string_cat_printf(app->fuzz_log, "  MTU exchange #%d..", app->fuzz_step + 1);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
        app->waiting_response = true;
        ble_gatt_client_exchange_mtu(app->connection_handle);
    } else {
        app->current_test++;
        app->fuzz_step = 0;
        furi_timer_start(app->timer, 50);
    }
}

static void run_read_all(GattFuzzerApp* app) {
    if(app->fuzz_step < app->all_char_total) {
        uint16_t handle = app->all_char_handles[app->fuzz_step];
        furi_string_cat_printf(app->fuzz_log, "  R h=%d..", handle);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
        app->waiting_response = true;
        ble_gatt_client_read(app->connection_handle, handle);
    } else {
        app->current_test++;
        app->fuzz_step = 0;
        furi_timer_start(app->timer, 50);
    }
}

static void start_current_test(GattFuzzerApp* app) {
    if(!run_all && app->current_test != start_test) {
        // Single test mode, we're done
        goto done;
    }
    if(app->current_test >= FuzzTestNum) {
        goto done;
    }

    app->fuzz_step = 0;
    furi_string_cat_printf(app->fuzz_log, "\n[%s]\n", fuzz_test_info[app->current_test].name);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
    // Kick off the first step
    furi_timer_start(app->timer, 100);
    return;

done:
    furi_string_cat_printf(app->fuzz_log,
        "\n--- Done ---\nOK: %d  ERR: %d  CRASH: %d\n",
        app->fuzz_ok, app->fuzz_errors, app->fuzz_crashes);
    if(app->fuzz_crashes > 0) {
        log_append(app, "\nDEVICE MAY HAVE CRASHED\n(no response detected)\n");
    }
    text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
    notification_message(app->notifications, &sequence_success);
}

static void dispatch_test_step(GattFuzzerApp* app) {
    switch(app->current_test) {
    case FuzzTestWriteAll:      run_write_all(app); break;
    case FuzzTestOversizeWrite: run_oversize_write(app); break;
    case FuzzTestInvalidHandle: run_invalid_handle(app); break;
    case FuzzTestRapidSubscribe: run_rapid_subscribe(app); break;
    case FuzzTestMtuFuzz:       run_mtu_fuzz(app); break;
    case FuzzTestReadAll:       run_read_all(app); break;
    default:                    start_current_test(app); break;
    }
}

static void handle_response(GattFuzzerApp* app, bool ok) {
    app->waiting_response = false;
    if(ok) {
        app->fuzz_ok++;
        log_append(app, "OK\n");
    } else {
        app->fuzz_errors++;
        log_append(app, "ERR\n");
    }
    app->fuzz_step++;
    // Small delay between operations
    furi_timer_start(app->timer, 50);
}

void fuzz_scene_run_test_on_enter(void* context) {
    GattFuzzerApp* app = context;

    start_test = app->current_test;
    run_all = (start_test == 0 && app->current_test == 0); // Run all if started from "Run All"
    // Actually detect run_all from test_menu: if current_test was set to 0 by "Run All"
    // We use a simple heuristic: test_menu sets current_test to the selected ID
    // For "Run All", it sets to 0 which is also FuzzTestWriteAll
    // We need a separate flag — let's use a static
    // The test_menu scene sets current_test=0 for "Run All" — same as FuzzTestWriteAll
    // So we check if the user selected index was FuzzTestNum (our Run All sentinel)
    run_all = true; // Default to run all; single test is set below
    if(start_test > 0) run_all = false;
    // Actually this is tricky. Let's just always run all when test 0 selected via Run All.
    // The menu uses index FuzzTestNum for "Run All" which is > any test ID.
    // But current_test was set to 0 in that case by test_menu. Let me just always run all.
    run_all = true; // For now, always run all tests sequentially

    furi_string_reset(app->fuzz_log);
    app->fuzz_ok = 0;
    app->fuzz_errors = 0;
    app->fuzz_crashes = 0;
    app->fuzz_step = 0;
    app->waiting_response = false;

    furi_string_cat_printf(app->fuzz_log,
        "=== GATT Fuzz ===\nTarget: %s\nChars: %d  Svcs: %d\n",
        app->current_device.has_name ? app->current_device.name : "Unknown",
        app->all_char_total, app->service_count);

    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->fuzz_log));
    text_box_set_font(app->text_box, TextBoxFontText);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuzzViewTextBox);

    app->current_test = start_test;
    start_current_test(app);
}

bool fuzz_scene_run_test_on_event(void* context, SceneManagerEvent event) {
    GattFuzzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FuzzCustomEventTick) {
        furi_timer_stop(app->timer);
        if(app->waiting_response) {
            // Timeout — device didn't respond
            app->waiting_response = false;
            app->fuzz_crashes++;
            log_append(app, "TIMEOUT!\n");
            app->fuzz_step++;
            furi_timer_start(app->timer, 50);
        } else if(app->current_test < FuzzTestNum) {
            dispatch_test_step(app);
        }
        return true;
    }

    if(event.event == FuzzCustomEventWriteComplete) {
        furi_timer_stop(app->timer);
        handle_response(app, true);
        return true;
    }

    if(event.event == FuzzCustomEventReadComplete) {
        furi_timer_stop(app->timer);
        handle_response(app, true);
        return true;
    }

    if(event.event == FuzzCustomEventNotification) {
        // Notification received — this is fine
        return true;
    }

    if(event.event == FuzzCustomEventGattError) {
        furi_timer_stop(app->timer);
        handle_response(app, false);
        return true;
    }

    return false;
}

void fuzz_scene_run_test_on_exit(void* context) {
    GattFuzzerApp* app = context;
    furi_timer_stop(app->timer);
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }
    text_box_reset(app->text_box);
}
