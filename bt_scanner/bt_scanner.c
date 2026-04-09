#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>

#define TAG "BtScanner"
#define MAX_DEVICES 64
#define SCAN_TIMEOUT_MS 10000

typedef struct {
    uint8_t address[6];
    int8_t rssi;
    char name[32];
    bool has_name;
} BtScanDevice;

typedef struct {
    BtScanDevice devices[MAX_DEVICES];
    uint8_t device_count;
    int16_t scroll_offset;
    bool scanning;
    FuriMutex* mutex;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} BtScannerApp;

// Parse advertisement data for device name (AD type 0x08 or 0x09)
static bool parse_adv_name(const uint8_t* data, uint8_t data_len, char* name, size_t name_size) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];
        if(type == 0x08 || type == 0x09) { // Shortened or Complete Local Name
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

// Check if device already discovered (by MAC address)
static int find_device(BtScannerApp* app, const uint8_t* address) {
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].address, address, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// Scan result callback — called from BLE thread context
static void scan_result_cb(GapScanResultData* result, void* context) {
    BtScannerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    int idx = find_device(app, result->address);

    if(idx >= 0) {
        // Update RSSI for existing device
        app->devices[idx].rssi = result->rssi;
        // Update name if we didn't have one before
        if(!app->devices[idx].has_name) {
            app->devices[idx].has_name = parse_adv_name(
                result->data, result->data_len, app->devices[idx].name,
                sizeof(app->devices[idx].name));
        }
    } else if(app->device_count < MAX_DEVICES) {
        // Add new device
        BtScanDevice* dev = &app->devices[app->device_count];
        memcpy(dev->address, result->address, 6);
        dev->rssi = result->rssi;
        dev->has_name = parse_adv_name(
            result->data, result->data_len, dev->name, sizeof(dev->name));
        app->device_count++;
    }

    furi_mutex_release(app->mutex);
}

static void draw_callback(Canvas* canvas, void* context) {
    BtScannerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    // Header
    if(app->scanning) {
        canvas_draw_str(canvas, 0, 10, "BT Scanner [scanning...]");
    } else {
        char header[32];
        snprintf(header, sizeof(header), "BT Scanner [%d found]", app->device_count);
        canvas_draw_str(canvas, 0, 10, header);
    }

    // Draw hint
    canvas_set_font(canvas, FontSecondary);
    if(!app->scanning && app->device_count == 0) {
        canvas_draw_str(canvas, 0, 30, "Press OK to start scan");
        canvas_draw_str(canvas, 0, 42, "Back to exit");
    }

    // Device list
    uint8_t y = 22;
    uint8_t visible_lines = 5;
    for(uint8_t i = app->scroll_offset;
        i < app->device_count && i < app->scroll_offset + visible_lines;
        i++) {
        BtScanDevice* dev = &app->devices[i];
        char line[64];
        if(dev->has_name) {
            snprintf(
                line,
                sizeof(line),
                "%ddBm %s",
                dev->rssi,
                dev->name);
        } else {
            snprintf(
                line,
                sizeof(line),
                "%ddBm %02X:%02X:%02X:%02X:%02X:%02X",
                dev->rssi,
                dev->address[5],
                dev->address[4],
                dev->address[3],
                dev->address[2],
                dev->address[1],
                dev->address[0]);
        }
        canvas_draw_str(canvas, 0, y, line);
        y += 10;
    }

    // Scroll indicators
    if(app->scroll_offset > 0) {
        canvas_draw_str(canvas, 120, 22, "^");
    }
    if(app->scroll_offset + visible_lines < app->device_count) {
        canvas_draw_str(canvas, 120, 62, "v");
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input_event, void* context) {
    BtScannerApp* app = context;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

static void bt_scanner_start_scan(BtScannerApp* app) {
    // Reset device list
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->device_count = 0;
    app->scroll_offset = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);

    // Set our callback to receive scan results
    furi_hal_bt_set_scan_callback(scan_result_cb, app);

    GapScanParams params = {
        .interval = 0x60,      // 60ms
        .window = 0x30,        // 30ms
        .active = true,        // Active scan to get names
        .timeout_ms = SCAN_TIMEOUT_MS,
    };
    if(!furi_hal_bt_start_scanning(&params)) {
        FURI_LOG_E(TAG, "Failed to start scanning");
        app->scanning = false;
        furi_hal_bt_set_scan_callback(NULL, NULL);
    }
}

int32_t bt_scanner_app(void* p) {
    UNUSED(p);

    // Check stack support
    if(!furi_hal_bt_is_gatt_gap_supported()) {
        FURI_LOG_E(TAG, "BLE GAP not supported");
        return -1;
    }

    BtScannerApp* app = malloc(sizeof(BtScannerApp));
    memset(app, 0, sizeof(BtScannerApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // GUI setup
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    FURI_LOG_I(TAG, "BT Scanner started");

    // Main loop
    InputEvent event;
    bool running = true;
    while(running) {
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, 100);

        if(status == FuriStatusOk) {
            if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
                switch(event.key) {
                case InputKeyBack:
                    if(app->scanning) {
                        furi_hal_bt_stop_scanning();
                    }
                    running = false;
                    break;
                case InputKeyOk:
                    if(!app->scanning) {
                        bt_scanner_start_scan(app);
                    } else {
                        furi_hal_bt_stop_scanning();
                    }
                    break;
                case InputKeyUp:
                    if(app->scroll_offset > 0) app->scroll_offset--;
                    break;
                case InputKeyDown:
                    if(app->scroll_offset < app->device_count - 1) app->scroll_offset++;
                    break;
                default:
                    break;
                }
            }
        }

        // Check scan completion
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
        }

        view_port_update(app->view_port);
    }

    // Cleanup
    furi_hal_bt_set_scan_callback(NULL, NULL);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
