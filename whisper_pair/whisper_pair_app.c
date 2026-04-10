#include "whisper_pair_app_i.h"

static bool wp_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    return scene_manager_handle_custom_event(((WhisperPairApp*)context)->scene_manager, event);
}

static bool wp_back_event_callback(void* context) {
    furi_assert(context);
    return scene_manager_handle_back_event(((WhisperPairApp*)context)->scene_manager);
}

// ── GAP scan callback ────────────────────────────────────────────────

void wp_scan_callback(GapScanResultData* result, void* context) {
    WhisperPairApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    WpAdvParseResult fp = wp_parse_fast_pair_adv(result->data, result->data_len);

    // Only track Fast Pair devices
    if(!fp.is_fast_pair) {
        furi_mutex_release(app->mutex);
        return;
    }

    // Update existing device
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].address, result->address, 6) == 0) {
            app->devices[i].rssi = result->rssi;
            if(!app->devices[i].has_name) {
                app->devices[i].has_name = wp_parse_name(
                    result->data, result->data_len, app->devices[i].name, WP_NAME_LEN);
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    // Add new Fast Pair device
    if(app->device_count < WP_MAX_DEVICES) {
        WpDevice* dev = &app->devices[app->device_count];
        memset(dev, 0, sizeof(WpDevice));
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->vuln_status = WpVulnUnknown;
        dev->has_model_id = true;
        dev->model_id = fp.model_id;
        dev->in_pairing_mode = fp.in_pairing_mode;
        dev->has_name = wp_parse_name(
            result->data, result->data_len, dev->name, WP_NAME_LEN);

        dev->known = wp_db_lookup(fp.model_id);
        if(dev->known && !dev->has_name) {
            strncpy(dev->name, dev->known->name, WP_NAME_LEN - 1);
            dev->has_name = true;
        }
        app->device_count++;
    }
    furi_mutex_release(app->mutex);
}

// ── GATT client callback ─────────────────────────────────────────────

static void wp_gatt_callback(BleGattClientEvent* event, void* context) {
    WhisperPairApp* app = context;
    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count = (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
            BLE_GATT_CLIENT_MAX_SERVICES : event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(app->view_dispatcher, WpCustomEventServicesDiscovered);
        break;
    case BleGattClientEventCharDiscoverComplete:
        app->char_count = (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
            BLE_GATT_CLIENT_MAX_CHARS : event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(app->view_dispatcher, WpCustomEventCharsDiscovered);
        break;
    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(app->view_dispatcher, WpCustomEventWriteComplete);
        break;
    case BleGattClientEventNotification:
        if(event->notification.data_len <= sizeof(app->kbp_response)) {
            memcpy(app->kbp_response, event->notification.data, event->notification.data_len);
            app->kbp_response_len = event->notification.data_len;
        }
        view_dispatcher_send_custom_event(app->view_dispatcher, WpCustomEventNotification);
        break;
    case BleGattClientEventError:
        view_dispatcher_send_custom_event(app->view_dispatcher, WpCustomEventGattError);
        break;
    default:
        break;
    }
}

// ── Timer callback ───────────────────────────────────────────────────

static void wp_timer_callback(void* context) {
    WhisperPairApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WpCustomEventTick);
}

// ── Scan view draw/input ─────────────────────────────────────────────

static const char* vuln_tag(WpVulnStatus s) {
    switch(s) {
    case WpVulnVulnerable: return " VULN";
    case WpVulnPatched: return " SAFE";
    case WpVulnTesting: return " TEST";
    case WpVulnError: return " ERR";
    default: return "";
    }
}

static void wp_scan_draw(Canvas* canvas, void* _model) {
    WpScanModel* model = _model;
    canvas_clear(canvas);

    // Header
    canvas_set_font(canvas, FontPrimary);
    if(model->scanning) {
        canvas_draw_str(canvas, 0, 10, "WhisperPair [scanning]");
    } else {
        uint8_t fp = 0;
        for(uint8_t i = 0; i < model->device_count; i++)
            if(model->devices[i].has_model_id) fp++;
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "WhisperPair [%d FP/%d]", fp, model->device_count);
        canvas_draw_str(canvas, 0, 10, hdr);
    }
    canvas_draw_line(canvas, 0, 12, 127, 12);

    if(model->device_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        if(!model->scanning) {
            canvas_draw_str(canvas, 0, 26, "CVE-2025-36911 scanner");
            canvas_draw_str(canvas, 0, 38, "Tests Fast Pair devices");
            canvas_draw_str(canvas, 0, 50, "for KBP vulnerability");
        } else {
            canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "Scanning...");
        }
        return;
    }

    canvas_set_font(canvas, FontSecondary);
    uint8_t visible = 4;
    uint8_t y = 22;
    for(uint8_t i = model->scroll;
        i < model->device_count && i < model->scroll + visible; i++) {
        const WpDevice* d = &model->devices[i];

        if(i == model->cursor) {
            canvas_draw_box(canvas, 0, y - 9, 128, 12);
            canvas_set_color(canvas, ColorWhite);
        }

        char line[44];
        if(d->has_model_id) {
            snprintf(line, sizeof(line), "%ddB %06lX %s%s",
                d->rssi, (unsigned long)d->model_id,
                d->has_name ? d->name : "FP Device",
                vuln_tag(d->vuln_status));
        } else {
            snprintf(line, sizeof(line), "%ddB %s",
                d->rssi, d->has_name ? d->name : "??");
        }
        canvas_draw_str(canvas, 2, y, line);

        if(i == model->cursor) {
            canvas_set_color(canvas, ColorBlack);
        }
        y += 12;
    }
}

static bool wp_scan_input(InputEvent* event, void* context) {
    WhisperPairApp* app = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyUp) {
        with_view_model(app->scan_view, WpScanModel * m, {
            if(m->cursor > 0) { m->cursor--; if(m->cursor < m->scroll) m->scroll = m->cursor; }
        }, true);
        consumed = true;
    } else if(event->key == InputKeyDown) {
        with_view_model(app->scan_view, WpScanModel * m, {
            if(m->cursor < m->device_count - 1) {
                m->cursor++;
                if(m->cursor >= m->scroll + 4) m->scroll = m->cursor - 3;
            }
        }, true);
        consumed = true;
    } else if(event->key == InputKeyOk) {
        bool has_devices = false;
        with_view_model(app->scan_view, WpScanModel * m, {
            if(m->device_count > 0) {
                app->selected_idx = m->cursor;
                has_devices = true;
            }
        }, false);
        if(has_devices) {
            scene_manager_next_scene(app->scene_manager, WhisperPairSceneDetail);
        }
        consumed = true;
    }
    return consumed;
}

// ── Alloc / Free ─────────────────────────────────────────────────────

WhisperPairApp* whisper_pair_app_alloc(void) {
    WhisperPairApp* app = malloc(sizeof(WhisperPairApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&whisper_pair_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, wp_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, wp_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WpViewSubmenu, submenu_get_view(app->submenu));

    app->scan_view = view_alloc();
    view_allocate_model(app->scan_view, ViewModelTypeLocking, sizeof(WpScanModel));
    view_set_context(app->scan_view, app);
    view_set_draw_callback(app->scan_view, wp_scan_draw);
    view_set_input_callback(app->scan_view, wp_scan_input);
    view_dispatcher_add_view(app->view_dispatcher, WpViewScanList, app->scan_view);

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WpViewWidget, widget_get_view(app->widget));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WpViewTextBox, text_box_get_view(app->text_box));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WpViewPopup, popup_get_view(app->popup));

    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WpViewLoading, loading_get_view(app->loading));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->device_count = 0;
    app->scanning = false;
    app->connected = false;
    app->test_log = furi_string_alloc();

    app->timer = furi_timer_alloc(wp_timer_callback, FuriTimerTypePeriodic, app);

    ble_gatt_client_init();
    ble_gatt_client_set_callback(wp_gatt_callback, app);

    return app;
}

void whisper_pair_app_free(WhisperPairApp* app) {
    furi_assert(app);

    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(NULL, NULL);
    if(app->scanning) { gap_stop_scanning(); app->scanning = false; }
    if(app->connected) { gap_disconnect(app->connection_handle); app->connected = false; }

    furi_timer_free(app->timer);
    furi_string_free(app->test_log);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, WpViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, WpViewScanList);
    view_free(app->scan_view);
    view_dispatcher_remove_view(app->view_dispatcher, WpViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, WpViewTextBox);
    text_box_free(app->text_box);
    view_dispatcher_remove_view(app->view_dispatcher, WpViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, WpViewLoading);
    loading_free(app->loading);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
}

int32_t whisper_pair_app(void* p) {
    UNUSED(p);
    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) gap_stop_scanning();

    WhisperPairApp* app = whisper_pair_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, WhisperPairSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    whisper_pair_app_free(app);
    return 0;
}
