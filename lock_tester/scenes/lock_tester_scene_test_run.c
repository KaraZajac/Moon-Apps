#include "../lock_tester_app_i.h"

// State machine for the test sequence
typedef enum {
    TestPhaseDiscoverChars,
    TestPhaseSubscribe,
    TestPhaseRunning,
    TestPhaseDone,
} TestPhase;

static TestPhase test_phase;

static void test_log_append(LockTesterApp* app, const char* fmt, ...) {
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

// Find the target characteristic for this lock profile
static bool find_target_char(LockTesterApp* app) {
    const LockProfile* profile = app->active_profile;

    // If profile specifies a characteristic UUID, find it
    if(profile->target_char_uuid != 0) {
        for(uint8_t i = 0; i < app->char_count; i++) {
            if(app->chars[i].uuid_type == 1 &&
               app->chars[i].uuid_16 == profile->target_char_uuid) {
                app->target_char_handle = app->chars[i].value_handle;
                return true;
            }
        }
    }

    // Otherwise find first writable characteristic
    for(uint8_t i = 0; i < app->char_count; i++) {
        if(app->chars[i].properties & 0x0C) { // Write or WriteNoResp
            app->target_char_handle = app->chars[i].value_handle;
            return true;
        }
    }

    return false;
}

// Find the right service to discover characteristics from
static int8_t find_target_service(LockTesterApp* app) {
    const LockProfile* profile = app->active_profile;

    // Match by service UUID if profile specifies one
    if(profile->service_uuid != 0) {
        for(uint8_t i = 0; i < app->service_count; i++) {
            if(app->services[i].uuid_type == 1 &&
               app->services[i].uuid_16 == profile->service_uuid) {
                return i;
            }
        }
    }

    // Fallback: first non-GAP/GATT service
    for(uint8_t i = 0; i < app->service_count; i++) {
        if(app->services[i].uuid_type == 1 &&
           app->services[i].uuid_16 != 0x1800 &&
           app->services[i].uuid_16 != 0x1801) {
            return i;
        }
    }
    return -1;
}

static void send_next_test(LockTesterApp* app) {
    if(app->current_test_idx >= app->active_profile->test_count) {
        test_phase = TestPhaseDone;
        test_log_append(app, "\n-- Tests complete --");
        notification_message(app->notifications, &sequence_double_vibro);
        return;
    }

    const LockTestCommand* cmd = &app->active_profile->tests[app->current_test_idx];

    // Log what we're sending
    FuriString* hex = furi_string_alloc();
    for(uint8_t i = 0; i < cmd->data_len; i++) {
        furi_string_cat_printf(hex, "%02X ", cmd->data[i]);
    }
    test_log_append(app, "[%d/%d] %s",
        app->current_test_idx + 1, app->active_profile->test_count, cmd->name);
    test_log_append(app, "  TX: %s", furi_string_get_cstr(hex));
    furi_string_free(hex);

    ble_gatt_client_write(
        app->connection_handle,
        app->target_char_handle,
        cmd->data,
        cmd->data_len);
}

void lock_tester_scene_test_run_on_enter(void* context) {
    LockTesterApp* app = context;

    furi_string_reset(app->test_log);
    app->current_test_idx = 0;
    app->has_notification = false;
    app->test_running = true;
    test_phase = TestPhaseDiscoverChars;

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    test_log_append(app, "Lock: %s", app->active_profile->name);
    test_log_append(app, "Finding target...");

    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewTextBox);

    // Find target service and discover its characteristics
    int8_t svc_idx = find_target_service(app);
    if(svc_idx < 0) {
        test_log_append(app, "ERROR: No matching service found");
        test_phase = TestPhaseDone;
        return;
    }

    test_log_append(app, "Service: 0x%04X", app->services[svc_idx].uuid_16);
    ble_gatt_client_discover_characteristics(
        app->connection_handle, &app->services[svc_idx]);
}

bool lock_tester_scene_test_run_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(test_phase) {
    case TestPhaseDiscoverChars:
        if(event.event == LockTesterCustomEventCharsDiscovered) {
            if(!find_target_char(app)) {
                test_log_append(app, "ERROR: No writable char");
                test_phase = TestPhaseDone;
                consumed = true;
                break;
            }
            test_log_append(app, "Target handle: 0x%04X", app->target_char_handle);

            // Try to subscribe to notifications on the char
            // (many locks send response via notification)
            test_phase = TestPhaseSubscribe;
            if(!ble_gatt_client_subscribe_notifications(
                   app->connection_handle, app->target_char_handle, true)) {
                // Subscribe failed — not all chars support it, proceed anyway
                test_log_append(app, "No notify support");
                test_phase = TestPhaseRunning;
                test_log_append(app, "\n-- Running tests --");
                send_next_test(app);
            }
            consumed = true;
        } else if(event.event == LockTesterCustomEventGattError) {
            test_log_append(app, "ERROR: Char discovery failed");
            test_phase = TestPhaseDone;
            consumed = true;
        }
        break;

    case TestPhaseSubscribe:
        if(event.event == LockTesterCustomEventWriteComplete) {
            test_log_append(app, "Subscribed to notify");
            test_phase = TestPhaseRunning;
            test_log_append(app, "\n-- Running tests --");
            send_next_test(app);
            consumed = true;
        } else if(event.event == LockTesterCustomEventGattError) {
            test_log_append(app, "No notify support");
            test_phase = TestPhaseRunning;
            test_log_append(app, "\n-- Running tests --");
            send_next_test(app);
            consumed = true;
        }
        break;

    case TestPhaseRunning:
        if(event.event == LockTesterCustomEventWriteComplete) {
            test_log_append(app, "  Write OK");

            // Brief delay before next test
            app->current_test_idx++;
            // Use timer for pacing between tests (500ms)
            furi_timer_start(app->timer, 500);
            consumed = true;
        } else if(event.event == LockTesterCustomEventNotification) {
            // Got a response from the lock!
            FuriString* hex = furi_string_alloc();
            for(uint16_t i = 0; i < app->notify_len; i++) {
                furi_string_cat_printf(hex, "%02X ", app->notify_buf[i]);
            }
            test_log_append(app, "  RX: %s", furi_string_get_cstr(hex));
            furi_string_free(hex);
            notification_message(app->notifications, &sequence_blink_green_10);
            consumed = true;
        } else if(event.event == LockTesterCustomEventGattError) {
            test_log_append(app, "  Write FAILED");
            app->current_test_idx++;
            furi_timer_start(app->timer, 500);
            consumed = true;
        } else if(event.event == LockTesterCustomEventTick) {
            furi_timer_stop(app->timer);
            send_next_test(app);
            consumed = true;
        }
        break;

    case TestPhaseDone:
        break;
    }

    return consumed;
}

void lock_tester_scene_test_run_on_exit(void* context) {
    LockTesterApp* app = context;
    furi_timer_stop(app->timer);
    app->test_running = false;
    text_box_reset(app->text_box);
}
