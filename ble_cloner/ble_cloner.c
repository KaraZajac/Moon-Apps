#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <gap.h>

#define TAG "BleCloner"
#define MAX_DEVICES 32
#define SCAN_TIMEOUT_MS 8000

typedef enum {
    ClonerViewScan,
    ClonerViewDetail,
    ClonerViewCloning,
} ClonerView;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[28];
    bool has_name;
    uint8_t adv_data[31];
    uint8_t adv_data_len;
} ClonerDevice;

typedef struct {
    ClonerDevice devices[MAX_DEVICES];
    uint8_t device_count;
    int16_t cursor;
    int16_t scroll;

    ClonerView view;
    bool scanning;
    bool cloning;

    FuriMutex* mutex;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} BleClonerApp;

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
    BleClonerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].address, result->address, 6) == 0) {
            app->devices[i].rssi = result->rssi;
            // Update adv data if longer
            if(result->data_len > app->devices[i].adv_data_len) {
                uint8_t copy_len = result->data_len > 31 ? 31 : result->data_len;
                memcpy(app->devices[i].adv_data, result->data, copy_len);
                app->devices[i].adv_data_len = copy_len;
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }
    if(app->device_count < MAX_DEVICES) {
        ClonerDevice* d = &app->devices[app->device_count];
        memcpy(d->address, result->address, 6);
        d->address_type = result->address_type;
        d->rssi = result->rssi;
        d->has_name = parse_name(result->data, result->data_len, d->name, sizeof(d->name));
        uint8_t copy_len = result->data_len > 31 ? 31 : result->data_len;
        memcpy(d->adv_data, result->data, copy_len);
        d->adv_data_len = copy_len;
        app->device_count++;
    }
    furi_mutex_release(app->mutex);
}

static void start_cloning(BleClonerApp* app) {
    ClonerDevice* d = &app->devices[app->cursor];

    // Stop any existing beacon
    if(furi_hal_bt_extra_beacon_is_active()) {
        furi_hal_bt_extra_beacon_stop();
    }

    // Configure beacon with cloned device's address
    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 100,
        .max_adv_interval_ms = 200,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_0dBm,
        .address_type = d->address_type == 0 ? GapAddressTypePublic : GapAddressTypeRandom,
    };
    memcpy(config.address, d->address, 6);

    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        FURI_LOG_E(TAG, "Failed to set beacon config");
        return;
    }

    if(d->adv_data_len > 0) {
        if(!furi_hal_bt_extra_beacon_set_data(d->adv_data, d->adv_data_len)) {
            FURI_LOG_E(TAG, "Failed to set beacon data");
            return;
        }
    }

    if(!furi_hal_bt_extra_beacon_start()) {
        FURI_LOG_E(TAG, "Failed to start beacon");
        return;
    }

    app->cloning = true;
    app->view = ClonerViewCloning;
    FURI_LOG_I(TAG, "Cloning %02X:%02X:%02X:%02X:%02X:%02X",
        d->address[5], d->address[4], d->address[3],
        d->address[2], d->address[1], d->address[0]);
}

static void draw_callback(Canvas* canvas, void* context) {
    BleClonerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    switch(app->view) {
    case ClonerViewScan: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10,
            app->scanning ? "BLE Cloner [scanning]" : "BLE Cloner");
        canvas_set_font(canvas, FontSecondary);
        if(app->device_count == 0 && !app->scanning) {
            canvas_draw_str(canvas, 0, 30, "OK: scan  Back: exit");
        } else if(!app->scanning) {
            canvas_draw_str(canvas, 90, 10, "OK:clone");
        }
        uint8_t y = 22;
        for(int i = app->scroll; i < app->device_count && y < 62; i++) {
            ClonerDevice* d = &app->devices[i];
            char line[40];
            snprintf(line, sizeof(line), "%s%ddB %s",
                i == app->cursor ? ">" : " ", d->rssi,
                d->has_name ? d->name : "??");
            canvas_draw_str(canvas, 0, y, line);
            y += 10;
        }
        break;
    }
    case ClonerViewDetail: {
        ClonerDevice* d = &app->devices[app->cursor];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, d->has_name ? d->name : "Unknown");
        canvas_set_font(canvas, FontSecondary);
        char addr[24];
        snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
            d->address[5], d->address[4], d->address[3],
            d->address[2], d->address[1], d->address[0]);
        canvas_draw_str(canvas, 0, 22, addr);
        char info[32];
        snprintf(info, sizeof(info), "RSSI: %d dBm  Data: %d B",
            d->rssi, d->adv_data_len);
        canvas_draw_str(canvas, 0, 34, info);
        // Hex dump of first 10 bytes of adv data
        char hex[50] = "";
        size_t pos = 0;
        uint8_t show = d->adv_data_len > 10 ? 10 : d->adv_data_len;
        for(uint8_t i = 0; i < show; i++)
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", d->adv_data[i]);
        canvas_draw_str(canvas, 0, 46, hex);
        canvas_draw_str(canvas, 0, 62, "OK: clone  Back: list");
        break;
    }
    case ClonerViewCloning: {
        ClonerDevice* d = &app->devices[app->cursor];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 20, 18, "CLONING");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 32, d->has_name ? d->name : "Unknown");
        char addr[24];
        snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
            d->address[5], d->address[4], d->address[3],
            d->address[2], d->address[1], d->address[0]);
        canvas_draw_str(canvas, 0, 44, addr);
        canvas_draw_str(canvas, 0, 62, "Back: stop cloning");
        break;
    }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    BleClonerApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

int32_t ble_cloner_app(void* p) {
    UNUSED(p);

    BleClonerApp* app = malloc(sizeof(BleClonerApp));
    memset(app, 0, sizeof(BleClonerApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

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
            switch(app->view) {
            case ClonerViewScan:
                if(event.key == InputKeyBack) {
                    if(app->scanning) gap_stop_scanning();
                    running = false;
                } else if(event.key == InputKeyOk) {
                    if(!app->scanning && app->device_count > 0) {
                        app->view = ClonerViewDetail;
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
                break;
            case ClonerViewDetail:
                if(event.key == InputKeyBack) {
                    app->view = ClonerViewScan;
                } else if(event.key == InputKeyOk) {
                    start_cloning(app);
                }
                break;
            case ClonerViewCloning:
                if(event.key == InputKeyBack) {
                    furi_hal_bt_extra_beacon_stop();
                    app->cloning = false;
                    app->view = ClonerViewScan;
                }
                break;
            }
            furi_mutex_release(app->mutex);
        }
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
        }
        view_port_update(app->view_port);
    }

    if(app->cloning) furi_hal_bt_extra_beacon_stop();
    gap_set_scan_callback(NULL, NULL);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
