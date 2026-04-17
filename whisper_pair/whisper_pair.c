#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <furi_ble/gatt_client.h>

#define TAG "WhisperPair"
#define MAX_DEVICES 32
#define SCAN_TIMEOUT_MS 10000

// Fast Pair service data identifier (little-endian of 0xFE2C)
#define FAST_PAIR_SVC_DATA_TYPE 0x16 // AD Type: Service Data - 16-bit UUID
#define FAST_PAIR_UUID_LE_LO    0x2C
#define FAST_PAIR_UUID_LE_HI    0xFE

typedef enum {
    WpVulnUnknown,
    WpVulnVulnerable,
    WpVulnPatched,
    WpVulnTesting,
    WpVulnError,
} WpVulnStatus;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[28];
    bool has_name;
    uint32_t model_id; // 3-byte Fast Pair Model ID
    bool has_model_id;
    bool is_known_vuln; // Model ID in known vulnerable list
    WpVulnStatus vuln_status;
} WpDevice;

// Known vulnerable Fast Pair Model IDs (from CVE-2025-36911 research)
typedef struct {
    uint32_t model_id;
    const char* name;
    bool vulnerable; // true = confirmed vuln, false = patched
} KnownFpDevice;

static const KnownFpDevice known_devices[] = {
    {0x30018E, "Pixel Buds Pro 2", true},
    {0xCD8256, "Sony WF-1000XM4", true},
    {0x0E30C3, "Sony WH-1000XM5", true},
    {0xD5BC6B, "Sony WH-1000XM6", true},
    {0x821F66, "Sony LinkBuds S", true},
    {0xF52494, "JBL Tune Buds", true},
    {0x718FA4, "JBL Live Pro 2", true},
    {0xD446A7, "JBL Tune Beam", true},
    {0x9D3F8A, "Soundcore Lib 4", true},
    {0xF0B77F, "Soundcore L4 NC", true},
    {0xD0A72C, "Nothing Ear (a)", true},
    {0xD97EBA, "OnePlus Buds 3P", true},
    {0xF00002, "Bose QC EarbudII", true},
    {0x0082DA, "Samsung (ptchd)", false},
    {0x00FA72, "Samsung (ptchd)", false},
};
#define KNOWN_DEVICE_COUNT (sizeof(known_devices) / sizeof(known_devices[0]))

typedef enum {
    WpViewScan,
    WpViewDetail,
    WpViewTesting,
} WpView;

typedef struct {
    WpDevice devices[MAX_DEVICES];
    uint8_t device_count;
    int16_t cursor;
    int16_t scroll;

    WpView view;
    bool scanning;
    bool testing;
    uint16_t connection_handle;
    char test_status[40];

    FuriMutex* mutex;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} WhisperPairApp;

// Look up Model ID in known vulnerable device database
static const KnownFpDevice* lookup_model_id(uint32_t model_id) {
    for(size_t i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        if(known_devices[i].model_id == model_id) {
            return &known_devices[i];
        }
    }
    return NULL;
}

// Parse Fast Pair service data from advertisement to extract 3-byte Model ID
static bool parse_fast_pair_model_id(const uint8_t* data, uint8_t len, uint32_t* model_id) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t ad_len = data[pos];
        if(ad_len == 0 || pos + ad_len >= len) break;
        uint8_t ad_type = data[pos + 1];
        // Look for Service Data (0x16) with Fast Pair UUID (0xFE2C)
        if(ad_type == FAST_PAIR_SVC_DATA_TYPE && ad_len >= 5) {
            if(data[pos + 2] == FAST_PAIR_UUID_LE_LO && data[pos + 3] == FAST_PAIR_UUID_LE_HI) {
                // Bytes 4-6 after UUID are the 3-byte Model ID (if in pairing mode)
                uint8_t svc_data_len = ad_len - 3; // subtract type + UUID
                if(svc_data_len >= 3) {
                    *model_id = ((uint32_t)data[pos + 4] << 16) |
                                ((uint32_t)data[pos + 5] << 8) |
                                (uint32_t)data[pos + 6];
                    return true;
                }
            }
        }
        pos += ad_len + 1;
    }
    return false;
}

// Parse device name from advertisement
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
    WhisperPairApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    // Try to parse Fast Pair Model ID
    uint32_t model_id = 0;
    bool has_model = parse_fast_pair_model_id(result->data, result->data_len, &model_id);

    // Check for duplicate
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].address, result->address, 6) == 0) {
            app->devices[i].rssi = result->rssi;
            if(has_model && !app->devices[i].has_model_id) {
                app->devices[i].model_id = model_id;
                app->devices[i].has_model_id = true;
                const KnownFpDevice* known = lookup_model_id(model_id);
                if(known) {
                    app->devices[i].is_known_vuln = known->vulnerable;
                    if(!app->devices[i].has_name) {
                        strncpy(app->devices[i].name, known->name, sizeof(app->devices[i].name) - 1);
                        app->devices[i].has_name = true;
                    }
                }
            }
            if(!app->devices[i].has_name) {
                app->devices[i].has_name = parse_name(
                    result->data, result->data_len, app->devices[i].name, sizeof(app->devices[i].name));
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    // Add new device (prioritize Fast Pair devices at the top)
    if(app->device_count < MAX_DEVICES) {
        WpDevice* dev;
        if(has_model) {
            // Insert Fast Pair devices before non-FP devices
            uint8_t insert_pos = 0;
            for(uint8_t i = 0; i < app->device_count; i++) {
                if(app->devices[i].has_model_id) insert_pos = i + 1;
            }
            // Shift down
            if(insert_pos < app->device_count) {
                memmove(&app->devices[insert_pos + 1], &app->devices[insert_pos],
                        (app->device_count - insert_pos) * sizeof(WpDevice));
            }
            dev = &app->devices[insert_pos];
        } else {
            dev = &app->devices[app->device_count];
        }
        memset(dev, 0, sizeof(WpDevice));
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_model_id = has_model;
        dev->model_id = model_id;
        dev->vuln_status = WpVulnUnknown;
        dev->has_name = parse_name(
            result->data, result->data_len, dev->name, sizeof(dev->name));

        if(has_model) {
            const KnownFpDevice* known = lookup_model_id(model_id);
            if(known) {
                dev->is_known_vuln = known->vulnerable;
                if(!dev->has_name) {
                    strncpy(dev->name, known->name, sizeof(dev->name) - 1);
                    dev->has_name = true;
                }
            }
        }
        app->device_count++;
    }
    furi_mutex_release(app->mutex);
}

// GATT client callback for vulnerability test
static void gatt_cb(BleGattClientEvent* event, void* context) {
    WhisperPairApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    WpDevice* dev = &app->devices[app->cursor];

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        // Look for Fast Pair service (0xFE2C)
        for(uint8_t i = 0; i < event->discover.count; i++) {
            if(event->discover.services[i].uuid_type == 1 &&
               event->discover.services[i].uuid_16 == 0xFE2C) {
                snprintf(app->test_status, sizeof(app->test_status), "FP service found, reading...");
                ble_gatt_client_discover_characteristics(
                    app->connection_handle, &event->discover.services[i]);
                furi_mutex_release(app->mutex);
                return;
            }
        }
        snprintf(app->test_status, sizeof(app->test_status), "No Fast Pair service");
        dev->vuln_status = WpVulnError;
        app->testing = false;
        break;

    case BleGattClientEventCharDiscoverComplete:
        // Look for Key-Based Pairing characteristic (UUID ending in 1234)
        for(uint8_t i = 0; i < event->char_discover.count; i++) {
            BleGattCharacteristic* c = &event->char_discover.chars[i];
            if(c->uuid_type == 2) {
                // Check last 2 bytes of 128-bit UUID for 0x1234
                if(c->uuid_128[12] == 0x34 && c->uuid_128[13] == 0x12) {
                    // Found KBP characteristic - write test request
                    snprintf(app->test_status, sizeof(app->test_status), "Testing KBP...");
                    uint8_t kbp_request[16] = {0};
                    kbp_request[0] = 0x00; // KBP request
                    kbp_request[1] = 0x11; // Flags: initiate bonding + extended response
                    // Bytes 2-7: target address (from advertisement)
                    memcpy(&kbp_request[2], dev->address, 6);
                    // Bytes 8-15: random salt
                    for(int j = 8; j < 16; j++) kbp_request[j] = rand() & 0xFF;

                    // Enable notifications first
                    ble_gatt_client_subscribe_notifications(
                        app->connection_handle, c->value_handle, true);
                    // Write the KBP request
                    ble_gatt_client_write(
                        app->connection_handle, c->value_handle, kbp_request, 16);
                    furi_mutex_release(app->mutex);
                    return;
                }
            }
        }
        snprintf(app->test_status, sizeof(app->test_status), "No KBP characteristic");
        dev->vuln_status = WpVulnError;
        app->testing = false;
        break;

    case BleGattClientEventNotification:
        // Got a response to KBP write - device is VULNERABLE
        snprintf(app->test_status, sizeof(app->test_status), "VULNERABLE - KBP responded");
        dev->vuln_status = WpVulnVulnerable;
        app->testing = false;
        furi_hal_bt_disconnect(app->connection_handle);
        break;

    case BleGattClientEventWriteComplete:
        // Write succeeded but no notification yet - wait a bit
        snprintf(app->test_status, sizeof(app->test_status), "KBP write OK, waiting...");
        break;

    case BleGattClientEventError:
        // Write rejected - device is PATCHED
        snprintf(app->test_status, sizeof(app->test_status), "PATCHED - KBP rejected");
        dev->vuln_status = WpVulnPatched;
        app->testing = false;
        furi_hal_bt_disconnect(app->connection_handle);
        break;

    default:
        break;
    }
    furi_mutex_release(app->mutex);
}

static const char* vuln_status_str(WpVulnStatus status) {
    switch(status) {
    case WpVulnVulnerable: return "[VULN]";
    case WpVulnPatched: return "[SAFE]";
    case WpVulnTesting: return "[TEST]";
    case WpVulnError: return "[ERR]";
    default: return "";
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    WhisperPairApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    switch(app->view) {
    case WpViewScan: {
        canvas_set_font(canvas, FontPrimary);
        if(app->scanning)
            canvas_draw_str(canvas, 0, 10, "WhisperPair [scanning]");
        else {
            // Count Fast Pair devices
            uint8_t fp_count = 0;
            for(uint8_t i = 0; i < app->device_count; i++)
                if(app->devices[i].has_model_id) fp_count++;
            char hdr[32];
            snprintf(hdr, sizeof(hdr), "WhisperPair [%d FP/%d]", fp_count, app->device_count);
            canvas_draw_str(canvas, 0, 10, hdr);
        }
        canvas_set_font(canvas, FontSecondary);
        if(app->device_count == 0 && !app->scanning) {
            canvas_draw_str(canvas, 0, 28, "Scans for Fast Pair devices");
            canvas_draw_str(canvas, 0, 40, "CVE-2025-36911 vuln test");
            canvas_draw_str(canvas, 0, 56, "OK: scan  Back: exit");
        }
        uint8_t y = 22;
        for(int i = app->scroll; i < app->device_count && y < 62; i++) {
            WpDevice* d = &app->devices[i];
            char line[42];
            if(d->has_model_id) {
                snprintf(line, sizeof(line), "%s%ddB %06lX %s %s",
                    i == app->cursor ? ">" : " ",
                    d->rssi,
                    d->model_id,
                    d->has_name ? d->name : "FP",
                    vuln_status_str(d->vuln_status));
            } else {
                snprintf(line, sizeof(line), "%s%ddB %s",
                    i == app->cursor ? ">" : " ",
                    d->rssi,
                    d->has_name ? d->name : "??");
            }
            canvas_draw_str(canvas, 0, y, line);
            y += 10;
        }
        break;
    }
    case WpViewDetail: {
        WpDevice* d = &app->devices[app->cursor];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, d->has_name ? d->name : "Unknown Device");
        canvas_set_font(canvas, FontSecondary);

        char addr[24];
        snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
            d->address[5], d->address[4], d->address[3],
            d->address[2], d->address[1], d->address[0]);
        canvas_draw_str(canvas, 0, 22, addr);

        if(d->has_model_id) {
            char model[32];
            snprintf(model, sizeof(model), "Model: %06lX  RSSI: %d",
                d->model_id, d->rssi);
            canvas_draw_str(canvas, 0, 34, model);

            const KnownFpDevice* known = lookup_model_id(d->model_id);
            if(known) {
                canvas_draw_str(canvas, 0, 46,
                    known->vulnerable ? "DB: Known VULNERABLE" : "DB: Known PATCHED");
            } else {
                canvas_draw_str(canvas, 0, 46, "DB: Unknown model");
            }
        } else {
            canvas_draw_str(canvas, 0, 34, "Not a Fast Pair device");
        }

        if(d->has_model_id) {
            canvas_draw_str(canvas, 0, 62, "OK: test vuln  Back: list");
        } else {
            canvas_draw_str(canvas, 0, 62, "Back: list");
        }
        break;
    }
    case WpViewTesting: {
        WpDevice* d = &app->devices[app->cursor];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Vulnerability Test");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 22, d->has_name ? d->name : "Unknown");

        char model[24];
        snprintf(model, sizeof(model), "Model: %06lX", d->model_id);
        canvas_draw_str(canvas, 0, 34, model);

        canvas_draw_str(canvas, 0, 48, app->test_status);

        if(!app->testing) {
            canvas_draw_str(canvas, 0, 62, "Back: return");
        }
        break;
    }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    WhisperPairApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

static void start_vuln_test(WhisperPairApp* app) {
    WpDevice* d = &app->devices[app->cursor];
    d->vuln_status = WpVulnTesting;
    app->testing = true;
    app->view = WpViewTesting;
    snprintf(app->test_status, sizeof(app->test_status), "Connecting...");

    ble_gatt_client_init();
    /* Per-connection callback registered when the connection handle arrives */
    furi_hal_bt_connect(d->address_type, d->address);
}

int32_t whisper_pair_app(void* p) {
    UNUSED(p);

    WhisperPairApp* app = malloc(sizeof(WhisperPairApp));
    memset(app, 0, sizeof(WhisperPairApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view = WpViewScan;

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
            case WpViewScan:
                if(event.key == InputKeyBack) {
                    if(app->scanning) furi_hal_bt_stop_scanning();
                    running = false;
                } else if(event.key == InputKeyOk) {
                    if(!app->scanning && app->device_count > 0) {
                        app->view = WpViewDetail;
                    } else if(!app->scanning) {
                        app->device_count = 0;
                        app->cursor = 0;
                        app->scroll = 0;
                        app->scanning = true;
                        furi_hal_bt_set_scan_callback(scan_cb, app);
                        GapScanParams sp = {.interval = 0x60, .window = 0x30,
                                            .active = true, .timeout_ms = SCAN_TIMEOUT_MS};
                        if(!furi_hal_bt_start_scanning(&sp)) app->scanning = false;
                    }
                } else if(event.key == InputKeyUp && app->cursor > 0) {
                    app->cursor--;
                    if(app->cursor < app->scroll) app->scroll = app->cursor;
                } else if(event.key == InputKeyDown && app->cursor < app->device_count - 1) {
                    app->cursor++;
                    if(app->cursor >= app->scroll + 4) app->scroll = app->cursor - 3;
                }
                break;

            case WpViewDetail:
                if(event.key == InputKeyBack) {
                    app->view = WpViewScan;
                } else if(event.key == InputKeyOk) {
                    WpDevice* d = &app->devices[app->cursor];
                    if(d->has_model_id) {
                        start_vuln_test(app);
                    }
                }
                break;

            case WpViewTesting:
                if(event.key == InputKeyBack && !app->testing) {
                    app->view = WpViewDetail;
                }
                break;
            }
            furi_mutex_release(app->mutex);
        }

        // Check scan completion
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
        }
        // Check connection for vuln test
        if(app->testing && gap_get_state() == GapStateConnected && app->connection_handle == 0) {
            app->connection_handle = gap_get_connection_handle();
            ble_gatt_client_set_callback(app->connection_handle, gatt_cb, app);
            if(app->connection_handle != 0xFFFF) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                snprintf(app->test_status, sizeof(app->test_status), "Connected, discovering...");
                furi_mutex_release(app->mutex);
                ble_gatt_client_discover_services(app->connection_handle);
            }
        }

        view_port_update(app->view_port);
    }

    furi_hal_bt_set_scan_callback(NULL, NULL);
    if(app->connection_handle) {
        ble_gatt_client_set_callback(app->connection_handle, NULL, NULL);
    }
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
