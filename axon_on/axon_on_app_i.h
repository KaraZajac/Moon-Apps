#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#include <gap.h>
#include <extra_beacon.h>

#include "scenes/axon_on_scene.h"

#define TAG "AxonOn"

// Axon body-camera OUI prefix: 00:25:DF
#define AXON_OUI_0 0x00
#define AXON_OUI_1 0x25
#define AXON_OUI_2 0xDF

// Axon BLE service UUID (16-bit)
#define AXON_SERVICE_UUID 0xFE6C

// Max cameras we track
#define AXON_MAX_DEVICES 16

// Scan parameters
#define AXON_SCAN_WINDOW_MS 10000
#define AXON_UI_REFRESH_MS  500

typedef enum {
    AxonOnViewSubmenu,
    AxonOnViewScanList,
    AxonOnViewWidget,
} AxonOnViewId;

typedef enum {
    AxonOnCustomEventTick,
} AxonOnCustomEvent;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[28];
    bool has_name;
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t hit_count;
    bool alerted;
} AxonDevice;

typedef struct {
    AxonDevice cameras[AXON_MAX_DEVICES];
    uint8_t camera_count;
    int16_t cursor;
    int16_t scroll;
    bool scanning;
    bool broadcasting;
    uint32_t scan_start_tick;
} AxonOnScanModel;

typedef struct AxonOnApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    // GUI modules
    Submenu* submenu;
    View* scan_view;
    Widget* widget;

    // Scan state (protected by mutex, written from GAP callback thread)
    AxonDevice cameras[AXON_MAX_DEVICES];
    uint8_t camera_count;
    FuriMutex* mutex;
    bool scanning;

    // Broadcast state
    bool broadcasting;
    bool beacon_was_active;
    GapExtraBeaconConfig saved_beacon_config;
    uint8_t saved_beacon_data[EXTRA_BEACON_MAX_DATA_SIZE];
    uint8_t saved_beacon_data_len;

    // Periodic timer
    FuriTimer* tick_timer;
    uint32_t scan_start_tick;
} AxonOnApp;
