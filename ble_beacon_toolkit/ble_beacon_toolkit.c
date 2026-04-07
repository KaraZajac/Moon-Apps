#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>

#define TAG "BleBeacon"

typedef enum {
    BeaconTypeIBeacon,
    BeaconTypeEddystoneURL,
    BeaconTypeCustom,
    BeaconTypeCount,
} BeaconType;

static const char* beacon_type_names[] = {
    "iBeacon",
    "Eddystone-URL",
    "Custom Data",
};

typedef enum {
    BeaconViewMenu,
    BeaconViewConfig,
    BeaconViewBroadcasting,
} BeaconView;

typedef struct {
    BeaconView view;
    BeaconType type;
    int16_t cursor;
    bool broadcasting;

    // iBeacon config
    uint8_t ibeacon_uuid[16];
    uint16_t ibeacon_major;
    uint16_t ibeacon_minor;
    int8_t ibeacon_tx_power;

    // Eddystone config
    char eddystone_url[18]; // Max URL after prefix

    // Custom data
    uint8_t custom_data[31];
    uint8_t custom_data_len;

    FuriMutex* mutex;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} BleBeaconApp;

// Build iBeacon advertisement data (Apple format)
static uint8_t build_ibeacon_data(BleBeaconApp* app, uint8_t* data) {
    // Flags
    data[0] = 0x02; // Length
    data[1] = 0x01; // AD Type: Flags
    data[2] = 0x06; // LE General Discoverable + BR/EDR Not Supported
    // Apple iBeacon prefix
    data[3] = 0x1A; // Length: 26
    data[4] = 0xFF; // AD Type: Manufacturer Specific
    data[5] = 0x4C; // Apple Inc (LSB)
    data[6] = 0x00; // Apple Inc (MSB)
    data[7] = 0x02; // iBeacon type
    data[8] = 0x15; // iBeacon length: 21
    // UUID (16 bytes)
    memcpy(&data[9], app->ibeacon_uuid, 16);
    // Major (big-endian)
    data[25] = app->ibeacon_major >> 8;
    data[26] = app->ibeacon_major & 0xFF;
    // Minor (big-endian)
    data[27] = app->ibeacon_minor >> 8;
    data[28] = app->ibeacon_minor & 0xFF;
    // TX Power
    data[29] = (uint8_t)app->ibeacon_tx_power;
    return 30;
}

// Build Eddystone-URL advertisement data
static uint8_t build_eddystone_url_data(BleBeaconApp* app, uint8_t* data) {
    // Flags
    data[0] = 0x02;
    data[1] = 0x01;
    data[2] = 0x06;
    // Complete 16-bit UUID: Eddystone (0xFEAA)
    data[3] = 0x03;
    data[4] = 0x03;
    data[5] = 0xAA;
    data[6] = 0xFE;
    // Eddystone-URL frame
    uint8_t url_len = strlen(app->eddystone_url);
    data[7] = url_len + 5; // Length
    data[8] = 0x16;        // AD Type: Service Data
    data[9] = 0xAA;        // Eddystone UUID (LSB)
    data[10] = 0xFE;       // Eddystone UUID (MSB)
    data[11] = 0x10;       // Frame type: URL
    data[12] = 0xF4;       // TX Power: -12 dBm
    data[13] = 0x00;       // URL Scheme: http://www.
    memcpy(&data[14], app->eddystone_url, url_len);
    return 14 + url_len;
}

static void start_beacon(BleBeaconApp* app) {
    if(furi_hal_bt_extra_beacon_is_active()) {
        furi_hal_bt_extra_beacon_stop();
    }

    // Random address for beacon
    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 200,
        .max_adv_interval_ms = 400,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_0dBm,
        .address_type = GapAddressTypeRandom,
        .address = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xFE},
    };

    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        FURI_LOG_E(TAG, "Failed to set beacon config");
        return;
    }

    uint8_t adv_data[31];
    uint8_t adv_len = 0;

    switch(app->type) {
    case BeaconTypeIBeacon:
        adv_len = build_ibeacon_data(app, adv_data);
        break;
    case BeaconTypeEddystoneURL:
        adv_len = build_eddystone_url_data(app, adv_data);
        break;
    case BeaconTypeCustom:
        adv_len = app->custom_data_len;
        memcpy(adv_data, app->custom_data, adv_len);
        break;
    default:
        return;
    }

    if(adv_len > 0) {
        if(!furi_hal_bt_extra_beacon_set_data(adv_data, adv_len)) {
            FURI_LOG_E(TAG, "Failed to set beacon data");
            return;
        }
        if(!furi_hal_bt_extra_beacon_start()) {
            FURI_LOG_E(TAG, "Failed to start beacon");
            return;
        }
        app->broadcasting = true;
        app->view = BeaconViewBroadcasting;
        FURI_LOG_I(TAG, "Broadcasting %s beacon (%d bytes)",
            beacon_type_names[app->type], adv_len);
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    BleBeaconApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    switch(app->view) {
    case BeaconViewMenu: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "BLE Beacon Toolkit");
        canvas_set_font(canvas, FontSecondary);
        for(int i = 0; i < BeaconTypeCount; i++) {
            char line[32];
            snprintf(line, sizeof(line), "%s %s",
                i == app->cursor ? ">" : " ", beacon_type_names[i]);
            canvas_draw_str(canvas, 0, 24 + i * 12, line);
        }
        canvas_draw_str(canvas, 0, 62, "OK: select  Back: exit");
        break;
    }
    case BeaconViewConfig: {
        canvas_set_font(canvas, FontPrimary);
        char title[32];
        snprintf(title, sizeof(title), "%s Config", beacon_type_names[app->type]);
        canvas_draw_str(canvas, 0, 10, title);
        canvas_set_font(canvas, FontSecondary);

        switch(app->type) {
        case BeaconTypeIBeacon: {
            char uuid_str[40];
            snprintf(uuid_str, sizeof(uuid_str), "UUID: %02X%02X%02X%02X...",
                app->ibeacon_uuid[0], app->ibeacon_uuid[1],
                app->ibeacon_uuid[2], app->ibeacon_uuid[3]);
            canvas_draw_str(canvas, 0, 22, uuid_str);
            char major_str[32];
            snprintf(major_str, sizeof(major_str), "Major: %d  Minor: %d",
                app->ibeacon_major, app->ibeacon_minor);
            canvas_draw_str(canvas, 0, 34, major_str);
            canvas_draw_str(canvas, 0, 46, "L/R: adjust  OK: broadcast");
            break;
        }
        case BeaconTypeEddystoneURL:
            canvas_draw_str(canvas, 0, 22, "http://www.");
            canvas_draw_str(canvas, 0, 34, app->eddystone_url);
            canvas_draw_str(canvas, 0, 46, "OK: broadcast");
            break;
        case BeaconTypeCustom:
            canvas_draw_str(canvas, 0, 22, "Flags + custom payload");
            char len_str[24];
            snprintf(len_str, sizeof(len_str), "Data length: %d bytes", app->custom_data_len);
            canvas_draw_str(canvas, 0, 34, len_str);
            canvas_draw_str(canvas, 0, 46, "OK: broadcast");
            break;
        default:
            break;
        }
        canvas_draw_str(canvas, 0, 62, "Back: menu");
        break;
    }
    case BeaconViewBroadcasting: {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 15, 20, "BROADCASTING");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 36, beacon_type_names[app->type]);
        canvas_draw_str(canvas, 0, 48, "Beacon is active");
        canvas_draw_str(canvas, 0, 62, "Back: stop");
        break;
    }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    BleBeaconApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

int32_t ble_beacon_toolkit_app(void* p) {
    UNUSED(p);

    BleBeaconApp* app = malloc(sizeof(BleBeaconApp));
    memset(app, 0, sizeof(BleBeaconApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // Default iBeacon UUID (Bloodmoon)
    uint8_t default_uuid[16] = {
        0xB1, 0x00, 0xD0, 0x00, 0x4D, 0x00, 0x00, 0x4E,
        0xBE, 0xAC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(app->ibeacon_uuid, default_uuid, 16);
    app->ibeacon_major = 1;
    app->ibeacon_minor = 1;
    app->ibeacon_tx_power = -59;

    // Default Eddystone URL
    strncpy(app->eddystone_url, "flipper.net", sizeof(app->eddystone_url));

    // Default custom data: just flags
    app->custom_data[0] = 0x02;
    app->custom_data[1] = 0x01;
    app->custom_data[2] = 0x06;
    app->custom_data_len = 3;

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
            case BeaconViewMenu:
                if(event.key == InputKeyBack) {
                    running = false;
                } else if(event.key == InputKeyOk) {
                    app->type = app->cursor;
                    app->view = BeaconViewConfig;
                } else if(event.key == InputKeyUp && app->cursor > 0) {
                    app->cursor--;
                } else if(event.key == InputKeyDown && app->cursor < BeaconTypeCount - 1) {
                    app->cursor++;
                }
                break;
            case BeaconViewConfig:
                if(event.key == InputKeyBack) {
                    app->view = BeaconViewMenu;
                } else if(event.key == InputKeyOk) {
                    start_beacon(app);
                } else if(event.key == InputKeyRight && app->type == BeaconTypeIBeacon) {
                    app->ibeacon_minor++;
                } else if(event.key == InputKeyLeft && app->type == BeaconTypeIBeacon) {
                    app->ibeacon_major++;
                }
                break;
            case BeaconViewBroadcasting:
                if(event.key == InputKeyBack) {
                    furi_hal_bt_extra_beacon_stop();
                    app->broadcasting = false;
                    app->view = BeaconViewMenu;
                }
                break;
            }
            furi_mutex_release(app->mutex);
        }
        view_port_update(app->view_port);
    }

    if(app->broadcasting) furi_hal_bt_extra_beacon_stop();
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
