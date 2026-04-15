#include "axon_on_app_i.h"

// ── Helpers ────────────────────────────────────────────────────────────────

static bool axon_on_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    AxonOnApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool axon_on_back_event_callback(void* context) {
    furi_assert(context);
    AxonOnApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void axon_on_tick_callback(void* context) {
    AxonOnApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, AxonOnCustomEventTick);
}

// ── Identify Axon camera ───────────────────────────────────────────────────
// Match by OUI prefix 00:25:DF OR service UUID 0xFE6C in AD data.

static bool axon_match_oui(const uint8_t* address) {
    return address[0] == AXON_OUI_0 &&
           address[1] == AXON_OUI_1 &&
           address[2] == AXON_OUI_2;
}

static bool axon_match_service_uuid(const uint8_t* data, uint8_t data_len) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];
        // Complete or Incomplete 16-bit UUID list
        if(type == 0x02 || type == 0x03) {
            for(uint8_t j = 2; j + 1 <= len; j += 2) {
                uint16_t uuid = (uint16_t)data[pos + j] | ((uint16_t)data[pos + j + 1] << 8);
                if(uuid == AXON_SERVICE_UUID) return true;
            }
        }
        // Service Data (16-bit UUID)
        if(type == 0x16 && len >= 3) {
            uint16_t uuid = (uint16_t)data[pos + 2] | ((uint16_t)data[pos + 3] << 8);
            if(uuid == AXON_SERVICE_UUID) return true;
        }
        pos += len + 1;
    }
    return false;
}

// Parse device name from raw AD data
static bool axon_parse_name(
    const uint8_t* data, uint8_t data_len, char* name, size_t name_size) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];
        if(type == 0x08 || type == 0x09) {
            uint8_t name_len = len - 1;
            if(name_len >= name_size) name_len = name_size - 1;
            memcpy(name, &data[pos + 2], name_len);
            name[name_len] = '\0';
            return true;
        }
        pos += len + 1;
    }
    return false;
}

// ── GAP scan callback (runs on BLE thread) ─────────────────────────────────

void axon_on_scan_callback(GapScanResultData* result, void* context) {
    AxonOnApp* app = context;

    bool is_axon = axon_match_oui(result->address);
    if(!is_axon && result->data && result->data_len > 0) {
        is_axon = axon_match_service_uuid(result->data, result->data_len);
    }
    if(!is_axon) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    uint32_t now = furi_get_tick();

    // Update existing camera
    for(uint8_t i = 0; i < app->camera_count; i++) {
        if(memcmp(app->cameras[i].address, result->address, 6) == 0) {
            app->cameras[i].rssi = result->rssi;
            app->cameras[i].last_seen = now;
            app->cameras[i].hit_count++;
            furi_mutex_release(app->mutex);
            return;
        }
    }

    // Add new camera
    if(app->camera_count < AXON_MAX_DEVICES) {
        AxonDevice* dev = &app->cameras[app->camera_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->first_seen = now;
        dev->last_seen = now;
        dev->hit_count = 1;
        dev->alerted = false;
        dev->has_name = false;
        dev->name[0] = '\0';

        if(result->data && result->data_len > 0) {
            dev->has_name = axon_parse_name(
                result->data, result->data_len, dev->name, sizeof(dev->name));
        }

        app->camera_count++;
    }

    furi_mutex_release(app->mutex);
}

// ── Axon advertisement packet ──────────────────────────────────────────────
// Service UUID 0xFE6C (little-endian) + 24-byte service data captured from
// a real Axon body camera.  Broadcasting this triggers nearby Axon cameras
// to begin recording.

static const uint8_t axon_adv_packet[] = {
    // AD struct 1: Flags (general discoverable, BR/EDR not supported)
    0x02, 0x01, 0x06,
    // AD struct 2: Complete 16-bit service UUID list — 0xFE6C
    0x03, 0x03, 0x6C, 0xFE,
    // AD struct 3: Service data for 0xFE6C
    0x1B, 0x16,             // length=27, type=Service Data (16-bit)
    0x6C, 0xFE,             // UUID 0xFE6C little-endian
    0x01, 0x58, 0x38, 0x37, 0x30, 0x30, 0x32, 0x46,  // "X87002FP4" serial
    0x50, 0x34, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
    0xCE, 0x1B, 0x33, 0x00, 0x00, 0x02, 0x00, 0x00,
};

// ── Extra beacon broadcast ─────────────────────────────────────────────────

bool axon_on_start_broadcast(AxonOnApp* app) {
    const GapExtraBeaconConfig* cfg = furi_hal_bt_extra_beacon_get_config();
    if(cfg) {
        app->saved_beacon_config = *cfg;
    }
    app->saved_beacon_data_len =
        furi_hal_bt_extra_beacon_get_data(app->saved_beacon_data);
    app->beacon_was_active = furi_hal_bt_extra_beacon_is_active();

    if(furi_hal_bt_extra_beacon_is_active()) {
        furi_hal_bt_extra_beacon_stop();
    }

    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 50,
        .max_adv_interval_ms = 75,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypePublic,
        .address = {AXON_OUI_0, AXON_OUI_1, AXON_OUI_2, 0xCA, 0xDA, 0xBA},
    };

    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        FURI_LOG_E(TAG, "Failed to set extra beacon config");
        return false;
    }
    if(!furi_hal_bt_extra_beacon_set_data(axon_adv_packet, sizeof(axon_adv_packet))) {
        FURI_LOG_E(TAG, "Failed to set extra beacon data");
        return false;
    }
    if(!furi_hal_bt_extra_beacon_start()) {
        FURI_LOG_E(TAG, "Failed to start extra beacon");
        return false;
    }

    FURI_LOG_I(TAG, "Broadcasting Axon command");
    return true;
}

void axon_on_stop_broadcast(AxonOnApp* app) {
    if(furi_hal_bt_extra_beacon_is_active()) {
        furi_hal_bt_extra_beacon_stop();
    }

    if(app->beacon_was_active) {
        furi_hal_bt_extra_beacon_set_config(&app->saved_beacon_config);
        furi_hal_bt_extra_beacon_set_data(
            app->saved_beacon_data, app->saved_beacon_data_len);
        furi_hal_bt_extra_beacon_start();
    }

    FURI_LOG_I(TAG, "Stopped broadcasting");
}

// ── Draw callback for scan view ────────────────────────────────────────────

static void axon_on_scan_draw_callback(Canvas* canvas, void* _model) {
    AxonOnScanModel* model = _model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    // Header line
    if(model->scanning) {
        char header[40];
        snprintf(header, sizeof(header), "Cameras: %d", model->camera_count);
        canvas_draw_str(canvas, 0, 10, header);

        // Scanning dots animation
        uint32_t elapsed =
            (furi_get_tick() - model->scan_start_tick) / furi_kernel_get_tick_frequency();
        uint8_t dots = (elapsed % 3) + 1;
        char dot_str[4] = "...";
        dot_str[dots] = '\0';
        canvas_draw_str(canvas, 80, 10, dot_str);
    } else {
        canvas_draw_str(canvas, 0, 10, "Scan stopped");
    }

    // Broadcast indicator
    if(model->broadcasting) {
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, "TX");
    }

    canvas_draw_line(canvas, 0, 12, 127, 12);

    if(model->camera_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 32, AlignCenter, AlignCenter, "Scanning for Axon");
        canvas_draw_str_aligned(
            canvas, 64, 44, AlignCenter, AlignCenter, "cameras...");
        canvas_draw_str_aligned(
            canvas, 64, 58, AlignCenter, AlignCenter, "OK: Start broadcast");
        return;
    }

    // Camera list — 4 visible rows
    canvas_set_font(canvas, FontSecondary);
    uint8_t visible = 4;
    uint8_t y = 22;

    for(uint8_t i = model->scroll;
        i < model->camera_count && i < model->scroll + visible; i++) {
        const AxonDevice* dev = &model->cameras[i];

        if(i == model->cursor) {
            canvas_draw_box(canvas, 0, y - 9, 128, 12);
            canvas_set_color(canvas, ColorWhite);
        }

        // MAC address
        char mac[20];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
            dev->address[0], dev->address[1], dev->address[2],
            dev->address[3], dev->address[4], dev->address[5]);
        canvas_draw_str(canvas, 2, y, mac);

        // RSSI
        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%ddB", dev->rssi);
        canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, rssi_str);

        if(i == model->cursor) {
            canvas_set_color(canvas, ColorBlack);
        }

        y += 12;
    }

    // Scroll indicators
    if(model->scroll > 0) {
        canvas_draw_str_aligned(canvas, 124, 14, AlignRight, AlignTop, "^");
    }
    if(model->scroll + visible < model->camera_count) {
        canvas_draw_str_aligned(canvas, 124, 62, AlignRight, AlignBottom, "v");
    }
}

// ── Input callback for scan view ───────────────────────────────────────────

static bool axon_on_scan_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    AxonOnApp* app = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                app->scan_view,
                AxonOnScanModel * model,
                {
                    if(model->cursor > 0) {
                        model->cursor--;
                        if(model->cursor < model->scroll) {
                            model->scroll = model->cursor;
                        }
                    }
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                app->scan_view,
                AxonOnScanModel * model,
                {
                    if(model->camera_count > 0 &&
                       model->cursor < model->camera_count - 1) {
                        model->cursor++;
                        if(model->cursor >= model->scroll + 4) {
                            model->scroll = model->cursor - 3;
                        }
                    }
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyOk) {
            // Toggle broadcast
            if(app->broadcasting) {
                axon_on_stop_broadcast(app);
                app->broadcasting = false;
                notification_message(app->notifications, &sequence_blink_blue_100);
            } else {
                if(axon_on_start_broadcast(app)) {
                    app->broadcasting = true;
                    notification_message(app->notifications, &sequence_blink_red_100);
                }
            }
            consumed = true;
        }
    }

    return consumed;
}

// ── Alloc / Free ───────────────────────────────────────────────────────────

AxonOnApp* axon_on_app_alloc(void) {
    AxonOnApp* app = malloc(sizeof(AxonOnApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&axon_on_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, axon_on_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, axon_on_back_event_callback);

    // Submenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AxonOnViewSubmenu, submenu_get_view(app->submenu));

    // Custom scan view
    app->scan_view = view_alloc();
    view_allocate_model(app->scan_view, ViewModelTypeLocking, sizeof(AxonOnScanModel));
    view_set_context(app->scan_view, app);
    view_set_draw_callback(app->scan_view, axon_on_scan_draw_callback);
    view_set_input_callback(app->scan_view, axon_on_scan_input_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, AxonOnViewScanList, app->scan_view);

    // Widget (for About)
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AxonOnViewWidget, widget_get_view(app->widget));

    // State init
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->camera_count = 0;
    app->scanning = false;
    app->broadcasting = false;
    app->beacon_was_active = false;

    app->tick_timer = furi_timer_alloc(
        axon_on_tick_callback, FuriTimerTypePeriodic, app);

    return app;
}

void axon_on_app_free(AxonOnApp* app) {
    furi_assert(app);

    gap_set_scan_callback(NULL, NULL);

    if(app->scanning) {
        gap_stop_scanning();
        app->scanning = false;
    }

    furi_timer_free(app->tick_timer);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, AxonOnViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, AxonOnViewScanList);
    view_free(app->scan_view);
    view_dispatcher_remove_view(app->view_dispatcher, AxonOnViewWidget);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

// ── Entry point ────────────────────────────────────────────────────────────

int32_t axon_on_app(void* p) {
    UNUSED(p);

    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) {
        gap_stop_scanning();
    }

    AxonOnApp* app = axon_on_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, AxonOnSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    axon_on_app_free(app);
    return 0;
}
