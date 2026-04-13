#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <gap.h>

#define TAG "BleRssiTracker"
#define MAX_DEVICES 32
#define RSSI_HISTORY 128
#define SCAN_TIMEOUT_MS 6000

typedef enum {
    TrackerViewScan,
    TrackerViewTrack,
} TrackerView;

typedef struct {
    uint8_t address[6];
    int8_t rssi;
    char name[28];
    bool has_name;
    uint8_t address_type;
} TrackerDevice;

typedef struct {
    TrackerDevice devices[MAX_DEVICES];
    uint8_t device_count;
    int16_t cursor;
    int16_t scroll;

    TrackerView view;
    bool scanning;
    bool tracking;

    // Tracking state
    uint8_t target_addr[6];
    char target_name[28];
    int8_t rssi_history[RSSI_HISTORY];
    uint8_t rssi_idx;
    uint8_t rssi_count;
    int8_t rssi_current;
    int8_t rssi_min;
    int8_t rssi_max;

    FuriMutex* mutex;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} BleRssiTrackerApp;

static bool parse_name(const uint8_t* data, uint8_t len, char* name, size_t sz) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t l = data[pos];
        if(l == 0 || pos + l >= len) break;
        if(data[pos + 1] == 0x08 || data[pos + 1] == 0x09) {
            uint8_t nl = l - 1;
            if(nl >= sz) nl = sz - 1;
            memcpy(name, &data[pos + 2], nl);
            name[nl] = '\0';
            return true;
        }
        pos += l + 1;
    }
    return false;
}

static void scan_cb(GapScanResultData* result, void* context) {
    BleRssiTrackerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    if(app->tracking) {
        // Only care about our target
        if(memcmp(result->address, app->target_addr, 6) == 0) {
            app->rssi_current = result->rssi;
            app->rssi_history[app->rssi_idx] = result->rssi;
            app->rssi_idx = (app->rssi_idx + 1) % RSSI_HISTORY;
            if(app->rssi_count < RSSI_HISTORY) app->rssi_count++;
            if(result->rssi < app->rssi_min) app->rssi_min = result->rssi;
            if(result->rssi > app->rssi_max) app->rssi_max = result->rssi;
        }
    } else {
        // Scan mode: add to device list
        for(uint8_t i = 0; i < app->device_count; i++) {
            if(memcmp(app->devices[i].address, result->address, 6) == 0) {
                app->devices[i].rssi = result->rssi;
                furi_mutex_release(app->mutex);
                return;
            }
        }
        if(app->device_count < MAX_DEVICES) {
            TrackerDevice* d = &app->devices[app->device_count];
            memcpy(d->address, result->address, 6);
            d->address_type = result->address_type;
            d->rssi = result->rssi;
            d->has_name = parse_name(result->data, result->data_len, d->name, sizeof(d->name));
            app->device_count++;
        }
    }
    furi_mutex_release(app->mutex);
}

static void draw_callback(Canvas* canvas, void* context) {
    BleRssiTrackerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    if(app->view == TrackerViewScan) {
        canvas_set_font(canvas, FontPrimary);
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "RSSI Tracker [%d]", app->device_count);
        canvas_draw_str(canvas, 0, 10, app->scanning ? "RSSI Tracker [scan]" : hdr);
        canvas_set_font(canvas, FontSecondary);
        if(app->device_count == 0 && !app->scanning)
            canvas_draw_str(canvas, 0, 30, "OK: scan  Back: exit");
        uint8_t y = 22;
        for(int i = app->scroll; i < app->device_count && y < 62; i++) {
            TrackerDevice* d = &app->devices[i];
            char line[40];
            snprintf(line, sizeof(line), "%s%ddB %s",
                i == app->cursor ? ">" : " ", d->rssi,
                d->has_name ? d->name : "??");
            canvas_draw_str(canvas, 0, y, line);
            y += 10;
        }
    } else {
        // Tracking view with RSSI graph
        canvas_set_font(canvas, FontPrimary);
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "%s", app->target_name[0] ? app->target_name : "Tracking");
        canvas_draw_str(canvas, 0, 10, hdr);

        // Current RSSI big display
        canvas_set_font(canvas, FontBigNumbers);
        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%d", app->rssi_current);
        canvas_draw_str(canvas, 0, 36, rssi_str);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 60, 28, "dBm");

        // Min/max
        char stats[32];
        snprintf(stats, sizeof(stats), "Min:%d Max:%d", app->rssi_min, app->rssi_max);
        canvas_draw_str(canvas, 0, 48, stats);

        // Signal bar (0 to 128px wide, mapped from -100 to -20 dBm)
        int bar_w = (app->rssi_current + 100) * 128 / 80;
        if(bar_w < 0) bar_w = 0;
        if(bar_w > 128) bar_w = 128;
        canvas_draw_box(canvas, 0, 52, bar_w, 6);
        canvas_draw_frame(canvas, 0, 52, 128, 6);

        // Mini graph (last 64 samples in bottom area)
        uint8_t graph_y = 60;
        uint8_t graph_h = 4;
        uint8_t samples = app->rssi_count > 64 ? 64 : app->rssi_count;
        for(uint8_t i = 0; i < samples; i++) {
            uint8_t idx = (app->rssi_idx - samples + i + RSSI_HISTORY) % RSSI_HISTORY;
            int h = (app->rssi_history[idx] + 100) * graph_h / 80;
            if(h < 0) h = 0;
            if(h > graph_h) h = graph_h;
            canvas_draw_dot(canvas, 64 + i, graph_y - h);
        }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    BleRssiTrackerApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

int32_t ble_rssi_tracker_app(void* p) {
    UNUSED(p);

    BleRssiTrackerApp* app = malloc(sizeof(BleRssiTrackerApp));
    memset(app, 0, sizeof(BleRssiTrackerApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->rssi_min = 0;
    app->rssi_max = -127;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, 100);
        if(status == FuriStatusOk && (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->view == TrackerViewScan) {
                if(event.key == InputKeyBack) {
                    if(app->scanning) gap_stop_scanning();
                    running = false;
                } else if(event.key == InputKeyOk) {
                    if(!app->scanning && app->cursor < app->device_count) {
                        // Start tracking selected device
                        TrackerDevice* d = &app->devices[app->cursor];
                        memcpy(app->target_addr, d->address, 6);
                        strncpy(app->target_name, d->has_name ? d->name : "", sizeof(app->target_name));
                        app->rssi_count = 0;
                        app->rssi_idx = 0;
                        app->rssi_min = 0;
                        app->rssi_max = -127;
                        app->rssi_current = d->rssi;
                        app->tracking = true;
                        app->view = TrackerViewTrack;
                        // Start continuous scanning
                        gap_set_scan_callback(scan_cb, app);
                        GapScanParams sp = {.interval = 0x30, .window = 0x20,
                                            .active = false, .timeout_ms = 0};
                        gap_start_scanning(&sp);
                    } else if(!app->scanning) {
                        app->device_count = 0;
                        app->cursor = 0;
                        app->scanning = true;
                        gap_set_scan_callback(scan_cb, app);
                        GapScanParams sp = {.interval = 0x60, .window = 0x30,
                                            .active = true, .timeout_ms = SCAN_TIMEOUT_MS};
                        gap_start_scanning(&sp);
                    }
                } else if(event.key == InputKeyUp && app->cursor > 0) {
                    app->cursor--;
                    if(app->cursor < app->scroll) app->scroll = app->cursor;
                } else if(event.key == InputKeyDown && app->cursor < app->device_count - 1) {
                    app->cursor++;
                    if(app->cursor >= app->scroll + 4) app->scroll = app->cursor - 3;
                }
            } else if(app->view == TrackerViewTrack) {
                if(event.key == InputKeyBack) {
                    gap_stop_scanning();
                    app->tracking = false;
                    app->view = TrackerViewScan;
                }
            }
            furi_mutex_release(app->mutex);
        }
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
        }
        view_port_update(app->view_port);
    }

    gap_set_scan_callback(NULL, NULL);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
