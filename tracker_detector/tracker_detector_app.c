#include "tracker_detector_app_i.h"
#include <gui/modules/loading.h>

static bool tracker_detector_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    TrackerDetectorApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool tracker_detector_back_event_callback(void* context) {
    furi_assert(context);
    TrackerDetectorApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

// GATT client callback — runs on BLE thread
static void tracker_detector_gatt_callback(BleGattClientEvent* event, void* context) {
    TrackerDetectorApp* app = context;

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        app->service_count =
            (event->discover.count > BLE_GATT_CLIENT_MAX_SERVICES) ?
                BLE_GATT_CLIENT_MAX_SERVICES :
                event->discover.count;
        memcpy(app->services, event->discover.services,
               app->service_count * sizeof(BleGattService));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, TrackerDetectorCustomEventServicesDiscovered);
        break;

    case BleGattClientEventCharDiscoverComplete:
        app->char_count =
            (event->char_discover.count > BLE_GATT_CLIENT_MAX_CHARS) ?
                BLE_GATT_CLIENT_MAX_CHARS :
                event->char_discover.count;
        memcpy(app->chars, event->char_discover.chars,
               app->char_count * sizeof(BleGattCharacteristic));
        view_dispatcher_send_custom_event(
            app->view_dispatcher, TrackerDetectorCustomEventCharsDiscovered);
        break;

    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, TrackerDetectorCustomEventWriteComplete);
        break;

    case BleGattClientEventError:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, TrackerDetectorCustomEventGattError);
        break;

    default:
        break;
    }
}


// Parse device name from raw AD data
static bool tracker_detector_parse_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size) {
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

// GAP scan callback — runs on BLE thread, must be fast
void tracker_detector_scan_callback(GapScanResultData* result, void* context) {
    TrackerDetectorApp* app = context;

    // Only process if this looks like a tracker
    TrackerSignatureResult sig = tracker_signature_identify(result->data, result->data_len);
    if(!sig.detected) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    uint32_t now = furi_get_tick();

    // Check if we already track this device
    for(uint8_t i = 0; i < app->tracker_count; i++) {
        if(memcmp(app->trackers[i].address, result->address, 6) == 0) {
            // Update existing
            app->trackers[i].rssi = result->rssi;
            app->trackers[i].last_seen = now;
            app->trackers[i].hit_count++;
            if(result->rssi < app->trackers[i].rssi_min)
                app->trackers[i].rssi_min = result->rssi;
            if(result->rssi > app->trackers[i].rssi_max)
                app->trackers[i].rssi_max = result->rssi;

            // Check following threshold
            uint32_t duration_s =
                (app->trackers[i].last_seen - app->trackers[i].first_seen) / furi_kernel_get_tick_frequency();
            if(duration_s >= TRACKER_FOLLOW_TIMEOUT_S) {
                app->trackers[i].following = true;
            }

            furi_mutex_release(app->mutex);
            return;
        }
    }

    // Add new tracker
    if(app->tracker_count < TRACKER_MAX_DEVICES) {
        TrackerDevice* dev = &app->trackers[app->tracker_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->type = sig.type;
        dev->rssi = result->rssi;
        dev->rssi_min = result->rssi;
        dev->rssi_max = result->rssi;
        dev->first_seen = now;
        dev->last_seen = now;
        dev->hit_count = 1;
        dev->following = false;
        dev->alerted = false;
        dev->has_name = false;
        dev->name[0] = '\0';

        if(result->data && result->data_len > 0) {
            dev->has_name = tracker_detector_parse_name(
                result->data, result->data_len, dev->name, TRACKER_NAME_LEN);
            dev->ad_data_len =
                (result->data_len > TRACKER_AD_DATA_MAX) ? TRACKER_AD_DATA_MAX : result->data_len;
            memcpy(dev->ad_data, result->data, dev->ad_data_len);
        } else {
            dev->ad_data_len = 0;
        }

        app->tracker_count++;
    }

    furi_mutex_release(app->mutex);
}

// Timer callback — fires periodically for UI refresh and state polling
static void tracker_detector_tick_callback(void* context) {
    TrackerDetectorApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, TrackerDetectorCustomEventTick);
}

// Custom scan view draw callback
static void tracker_detector_scan_draw_callback(Canvas* canvas, void* _model) {
    TrackerDetectorScanModel* model = _model;

    canvas_clear(canvas);

    // Header
    canvas_set_font(canvas, FontPrimary);
    if(model->scanning) {
        char header[32];
        snprintf(header, sizeof(header), "Trackers: %d", model->tracker_count);
        canvas_draw_str(canvas, 0, 10, header);

        // Scanning indicator animation
        uint32_t elapsed = (furi_get_tick() - model->scan_start_tick) / furi_kernel_get_tick_frequency();
        uint8_t dots = (elapsed % 3) + 1;
        char dot_str[4] = "...";
        dot_str[dots] = '\0';
        canvas_draw_str(canvas, 100, 10, dot_str);
    } else {
        canvas_draw_str(canvas, 0, 10, "Scan stopped");
    }

    canvas_draw_line(canvas, 0, 12, 127, 12);

    if(model->tracker_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "No trackers detected");
        return;
    }

    // Device list — 4 visible rows
    canvas_set_font(canvas, FontSecondary);
    uint8_t visible = 4;
    uint8_t y = 22;

    for(uint8_t i = model->scroll; i < model->tracker_count && i < model->scroll + visible; i++) {
        const TrackerDevice* dev = &model->trackers[i];

        // Cursor indicator
        if(i == model->cursor) {
            canvas_draw_box(canvas, 0, y - 9, 128, 12);
            canvas_set_color(canvas, ColorWhite);
        }

        // Following alert indicator
        if(dev->following) {
            canvas_draw_str(canvas, 0, y, "!");
        }

        // Tracker type
        const char* type_name = tracker_type_get_name(dev->type);
        canvas_draw_str(canvas, dev->following ? 8 : 2, y, type_name);

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
    if(model->scroll + visible < model->tracker_count) {
        canvas_draw_str_aligned(canvas, 124, 62, AlignRight, AlignBottom, "v");
    }
}

// Custom scan view input callback
static bool tracker_detector_scan_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    TrackerDetectorApp* app = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                app->scan_view,
                TrackerDetectorScanModel * model,
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
                TrackerDetectorScanModel * model,
                {
                    if(model->cursor < model->tracker_count - 1) {
                        model->cursor++;
                        if(model->cursor >= model->scroll + 4) {
                            model->scroll = model->cursor - 3;
                        }
                    }
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyOk) {
            // Select tracker for detail view
            with_view_model(
                app->scan_view,
                TrackerDetectorScanModel * model,
                {
                    if(model->tracker_count > 0) {
                        app->selected_tracker_idx = model->cursor;
                    }
                },
                false);
            if(app->tracker_count > 0) {
                scene_manager_next_scene(
                    app->scene_manager, TrackerDetectorSceneDetail);
            }
            consumed = true;
        }
    }

    return consumed;
}

TrackerDetectorApp* tracker_detector_app_alloc(void) {
    TrackerDetectorApp* app = malloc(sizeof(TrackerDetectorApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&tracker_detector_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, tracker_detector_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, tracker_detector_back_event_callback);

    // Submenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TrackerDetectorViewSubmenu, submenu_get_view(app->submenu));

    // Custom scan list view
    app->scan_view = view_alloc();
    view_allocate_model(app->scan_view, ViewModelTypeLocking, sizeof(TrackerDetectorScanModel));
    view_set_context(app->scan_view, app);
    view_set_draw_callback(app->scan_view, tracker_detector_scan_draw_callback);
    view_set_input_callback(app->scan_view, tracker_detector_scan_input_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, TrackerDetectorViewScanList, app->scan_view);

    // Widget (for detail view)
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TrackerDetectorViewWidget, widget_get_view(app->widget));

    // Popup
    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TrackerDetectorViewPopup, popup_get_view(app->popup));

    // Loading
    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TrackerDetectorViewLoading, loading_get_view(app->loading));

    // State init
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->tracker_count = 0;
    app->scanning = false;
    app->connected = false;
    app->selected_tracker_idx = 0;

    app->tick_timer = furi_timer_alloc(
        tracker_detector_tick_callback, FuriTimerTypePeriodic, app);

    // GATT client
    ble_gatt_client_init();
    ble_gatt_client_set_callback(tracker_detector_gatt_callback, app);

    return app;
}

void tracker_detector_app_free(TrackerDetectorApp* app) {
    furi_assert(app);

    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(NULL, NULL);

    if(app->scanning) {
        gap_stop_scanning();
        app->scanning = false;
    }
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }

    furi_timer_free(app->tick_timer);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, TrackerDetectorViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, TrackerDetectorViewScanList);
    view_free(app->scan_view);
    view_dispatcher_remove_view(app->view_dispatcher, TrackerDetectorViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, TrackerDetectorViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, TrackerDetectorViewLoading);
    loading_free(app->loading);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t tracker_detector_app(void* p) {
    UNUSED(p);

    // Ensure clean BLE state
    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) {
        gap_stop_scanning();
    }

    TrackerDetectorApp* app = tracker_detector_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, TrackerDetectorSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    tracker_detector_app_free(app);
    return 0;
}
