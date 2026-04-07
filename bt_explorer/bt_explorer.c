#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <furi_ble/gatt_client.h>

#define TAG "BtExplorer"
#define MAX_DEVICES 32
#define SCAN_TIMEOUT_MS 8000

typedef enum {
    ExplorerViewScan,
    ExplorerViewServices,
    ExplorerViewChars,
    ExplorerViewCharDetail,
} ExplorerView;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[28];
    bool has_name;
} ExplorerDevice;

typedef struct {
    ExplorerDevice devices[MAX_DEVICES];
    uint8_t device_count;
    int16_t cursor;
    int16_t scroll;

    ExplorerView view;
    bool scanning;
    bool connecting;
    bool connected;
    uint16_t connection_handle;

    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    uint8_t read_buf[64];
    uint16_t read_len;
    bool has_read_data;

    FuriMutex* mutex;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} BtExplorerApp;

static bool parse_adv_name(const uint8_t* data, uint8_t len, char* name, size_t sz) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t l = data[pos];
        if(l == 0 || pos + l >= len) break;
        uint8_t type = data[pos + 1];
        if(type == 0x08 || type == 0x09) {
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
    BtExplorerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    // Check duplicate
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].address, result->address, 6) == 0) {
            app->devices[i].rssi = result->rssi;
            if(!app->devices[i].has_name) {
                app->devices[i].has_name = parse_adv_name(
                    result->data, result->data_len, app->devices[i].name, sizeof(app->devices[i].name));
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }
    if(app->device_count < MAX_DEVICES) {
        ExplorerDevice* dev = &app->devices[app->device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = parse_adv_name(
            result->data, result->data_len, dev->name, sizeof(dev->name));
        app->device_count++;
    }
    furi_mutex_release(app->mutex);
}

static void gatt_cb(BleGattClientEvent* event, void* context) {
    BtExplorerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        memcpy(app->services, event->discover.services,
               event->discover.count * sizeof(BleGattService));
        app->service_count = event->discover.count;
        app->view = ExplorerViewServices;
        app->cursor = 0;
        app->scroll = 0;
        break;
    case BleGattClientEventCharDiscoverComplete:
        memcpy(app->chars, event->char_discover.chars,
               event->char_discover.count * sizeof(BleGattCharacteristic));
        app->char_count = event->char_discover.count;
        app->view = ExplorerViewChars;
        app->cursor = 0;
        app->scroll = 0;
        break;
    case BleGattClientEventReadComplete:
        app->read_len = event->read.data_len > 64 ? 64 : event->read.data_len;
        memcpy(app->read_buf, event->read.data, app->read_len);
        app->has_read_data = true;
        app->view = ExplorerViewCharDetail;
        break;
    default:
        break;
    }
    furi_mutex_release(app->mutex);
}

static void draw_callback(Canvas* canvas, void* context) {
    BtExplorerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    switch(app->view) {
    case ExplorerViewScan: {
        canvas_set_font(canvas, FontPrimary);
        if(app->scanning)
            canvas_draw_str(canvas, 0, 10, "BT Explorer [scanning]");
        else if(app->connecting)
            canvas_draw_str(canvas, 0, 10, "BT Explorer [connecting]");
        else {
            char hdr[32];
            snprintf(hdr, sizeof(hdr), "BT Explorer [%d]", app->device_count);
            canvas_draw_str(canvas, 0, 10, hdr);
        }
        canvas_set_font(canvas, FontSecondary);
        if(app->device_count == 0 && !app->scanning) {
            canvas_draw_str(canvas, 0, 30, "OK: scan  Back: exit");
        }
        uint8_t y = 22;
        for(int i = app->scroll; i < app->device_count && y < 62; i++) {
            ExplorerDevice* d = &app->devices[i];
            char line[40];
            if(d->has_name)
                snprintf(line, sizeof(line), "%s%ddB %s",
                    i == app->cursor ? ">" : " ", d->rssi, d->name);
            else
                snprintf(line, sizeof(line), "%s%ddB %02X:%02X:%02X",
                    i == app->cursor ? ">" : " ", d->rssi,
                    d->address[5], d->address[4], d->address[3]);
            canvas_draw_str(canvas, 0, y, line);
            y += 10;
        }
        break;
    }
    case ExplorerViewServices: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Services");
        canvas_set_font(canvas, FontSecondary);
        uint8_t y = 22;
        for(int i = app->scroll; i < app->service_count && y < 62; i++) {
            char line[40];
            BleGattService* s = &app->services[i];
            if(s->uuid_type == 1)
                snprintf(line, sizeof(line), "%s0x%04X [%04X-%04X]",
                    i == app->cursor ? ">" : " ", s->uuid_16, s->start_handle, s->end_handle);
            else
                snprintf(line, sizeof(line), "%s%02X%02X.. [%04X-%04X]",
                    i == app->cursor ? ">" : " ",
                    s->uuid_128[15], s->uuid_128[14], s->start_handle, s->end_handle);
            canvas_draw_str(canvas, 0, y, line);
            y += 10;
        }
        break;
    }
    case ExplorerViewChars: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Characteristics");
        canvas_set_font(canvas, FontSecondary);
        uint8_t y = 22;
        for(int i = app->scroll; i < app->char_count && y < 62; i++) {
            char line[40];
            BleGattCharacteristic* c = &app->chars[i];
            char props[8] = "";
            if(c->properties & 0x02) strcat(props, "R");
            if(c->properties & 0x08) strcat(props, "W");
            if(c->properties & 0x10) strcat(props, "N");
            if(c->uuid_type == 1)
                snprintf(line, sizeof(line), "%s0x%04X [%s]",
                    i == app->cursor ? ">" : " ", c->uuid_16, props);
            else
                snprintf(line, sizeof(line), "%s%02X%02X.. [%s]",
                    i == app->cursor ? ">" : " ",
                    c->uuid_128[15], c->uuid_128[14], props);
            canvas_draw_str(canvas, 0, y, line);
            y += 10;
        }
        break;
    }
    case ExplorerViewCharDetail: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Value");
        canvas_set_font(canvas, FontSecondary);
        if(app->has_read_data) {
            // Hex dump
            char hex[40];
            uint8_t show = app->read_len > 16 ? 16 : app->read_len;
            size_t pos = 0;
            for(uint8_t i = 0; i < show && pos < sizeof(hex) - 3; i++) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", app->read_buf[i]);
            }
            canvas_draw_str(canvas, 0, 22, hex);
            // ASCII
            char ascii[20];
            for(uint8_t i = 0; i < show && i < 19; i++) {
                ascii[i] = (app->read_buf[i] >= 0x20 && app->read_buf[i] < 0x7F) ?
                    app->read_buf[i] : '.';
            }
            ascii[show > 19 ? 19 : show] = '\0';
            canvas_draw_str(canvas, 0, 34, ascii);
            char info[32];
            snprintf(info, sizeof(info), "%d bytes", app->read_len);
            canvas_draw_str(canvas, 0, 46, info);
        } else {
            canvas_draw_str(canvas, 0, 30, "Reading...");
        }
        canvas_draw_str(canvas, 0, 62, "Back: return");
        break;
    }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    BtExplorerApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

int32_t bt_explorer_app(void* p) {
    UNUSED(p);

    BtExplorerApp* app = malloc(sizeof(BtExplorerApp));
    memset(app, 0, sizeof(BtExplorerApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view = ExplorerViewScan;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    ble_gatt_client_init();
    ble_gatt_client_set_callback(gatt_cb, app);

    InputEvent event;
    bool running = true;
    while(running) {
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, 100);
        if(status == FuriStatusOk && (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            switch(app->view) {
            case ExplorerViewScan:
                if(event.key == InputKeyBack) {
                    if(app->scanning) furi_hal_bt_stop_scanning();
                    running = false;
                } else if(event.key == InputKeyOk) {
                    if(!app->scanning && !app->connecting && app->cursor < app->device_count) {
                        // Connect to selected device
                        app->connecting = true;
                        ExplorerDevice* d = &app->devices[app->cursor];
                        furi_hal_bt_connect(d->address_type, d->address);
                    } else if(!app->scanning) {
                        // Start scan
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

            case ExplorerViewServices:
                if(event.key == InputKeyBack) {
                    app->view = ExplorerViewScan;
                    furi_hal_bt_disconnect(app->connection_handle);
                    app->connected = false;
                } else if(event.key == InputKeyOk && app->cursor < app->service_count) {
                    ble_gatt_client_discover_characteristics(
                        app->connection_handle, &app->services[app->cursor]);
                } else if(event.key == InputKeyUp && app->cursor > 0) {
                    app->cursor--;
                    if(app->cursor < app->scroll) app->scroll = app->cursor;
                } else if(event.key == InputKeyDown && app->cursor < app->service_count - 1) {
                    app->cursor++;
                    if(app->cursor >= app->scroll + 4) app->scroll = app->cursor - 3;
                }
                break;

            case ExplorerViewChars:
                if(event.key == InputKeyBack) {
                    app->view = ExplorerViewServices;
                    app->cursor = 0;
                    app->scroll = 0;
                } else if(event.key == InputKeyOk && app->cursor < app->char_count) {
                    BleGattCharacteristic* c = &app->chars[app->cursor];
                    if(c->properties & 0x02) { // Readable
                        app->has_read_data = false;
                        ble_gatt_client_read(app->connection_handle, c->value_handle);
                    }
                } else if(event.key == InputKeyUp && app->cursor > 0) {
                    app->cursor--;
                    if(app->cursor < app->scroll) app->scroll = app->cursor;
                } else if(event.key == InputKeyDown && app->cursor < app->char_count - 1) {
                    app->cursor++;
                    if(app->cursor >= app->scroll + 4) app->scroll = app->cursor - 3;
                }
                break;

            case ExplorerViewCharDetail:
                if(event.key == InputKeyBack) {
                    app->view = ExplorerViewChars;
                }
                break;
            }
            furi_mutex_release(app->mutex);
        }
        // Check scan timeout
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
        }
        // Check connection completion
        if(app->connecting) {
            GapState state = gap_get_state();
            if(state == GapStateConnected) {
                app->connecting = false;
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                // Start service discovery
                ble_gatt_client_discover_services(app->connection_handle);
            } else if(state == GapStateIdle) {
                // Connection failed
                app->connecting = false;
            }
        }
        view_port_update(app->view_port);
    }

    if(app->connected) {
        furi_hal_bt_disconnect(app->connection_handle);
    }
    furi_hal_bt_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(NULL, NULL);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
