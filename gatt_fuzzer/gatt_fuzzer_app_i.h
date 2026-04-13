#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <gui/modules/loading.h>
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>

#include <gap.h>
#include <furi_ble/gatt_client.h>

#include "scenes/fuzz_scene.h"
#include "helpers/fuzz_custom_event.h"
#include "helpers/fuzz_tests.h"

#define TAG "GattFuzzer"

#define FUZZ_MAX_SCAN_DEVICES 48
#define FUZZ_DEVICE_NAME_LEN  32

typedef enum {
    FuzzViewSubmenu,
    FuzzViewWidget,
    FuzzViewPopup,
    FuzzViewLoading,
    FuzzViewTextBox,
} FuzzViewId;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[FUZZ_DEVICE_NAME_LEN];
    bool has_name;
} FuzzDevice;

typedef struct GattFuzzerApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    TextBox* text_box;

    // Scan
    FuzzDevice scan_devices[FUZZ_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    FuriMutex* mutex;
    bool scanning;

    // Connection
    bool connected;
    uint16_t connection_handle;
    uint32_t connect_poll_count;
    FuriTimer* timer;

    // Current device
    FuzzDevice current_device;

    // GATT
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;
    uint8_t total_char_count; // across all services
    bool discovery_done;

    // All discovered characteristic handles (across all services)
    uint16_t all_char_handles[128];
    uint8_t all_char_properties[128]; // BLE property flags
    uint8_t all_char_total;

    // Fuzz state
    FuzzTestId current_test;
    uint16_t fuzz_step;
    uint16_t fuzz_errors;
    uint16_t fuzz_crashes; // no-response count
    uint16_t fuzz_ok;
    bool waiting_response;
    FuriString* fuzz_log;

    // PRNG state
    uint32_t rng_state;
} GattFuzzerApp;

void fuzz_scan_callback(GapScanResultData* result, void* context);
bool fuzz_parse_adv_name(const uint8_t* data, uint8_t data_len, char* name, size_t name_size);

// Simple PRNG for reproducible fuzz data
static inline uint32_t fuzz_rand(GattFuzzerApp* app) {
    app->rng_state = app->rng_state * 1103515245 + 12345;
    return (app->rng_state >> 16) & 0x7FFF;
}
