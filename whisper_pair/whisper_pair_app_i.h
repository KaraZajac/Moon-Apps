#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_box.h>
#include <gui/modules/loading.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#include <gap.h>
#include <furi_ble/gatt_client.h>

#include "scenes/whisper_pair_scene.h"
#include "helpers/whisper_pair_custom_event.h"
#include "helpers/whisper_pair_db.h"

#define TAG "WhisperPair"

#define WP_MAX_DEVICES 32
#define WP_NAME_LEN    28
#define WP_KBP_TIMEOUT_MS 5000

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
    char name[WP_NAME_LEN];
    bool has_name;
    uint32_t model_id;
    bool has_model_id;
    bool in_pairing_mode;
    const WpKnownDevice* known;
    WpVulnStatus vuln_status;
} WpDevice;

typedef enum {
    WpViewSubmenu,
    WpViewScanList,
    WpViewWidget,
    WpViewTextBox,
    WpViewPopup,
    WpViewLoading,
} WpViewId;

// Model for the custom scan list view
typedef struct {
    WpDevice devices[WP_MAX_DEVICES];
    uint8_t device_count;
    int16_t cursor;
    int16_t scroll;
    bool scanning;
} WpScanModel;

typedef enum {
    TestPhaseConnecting,
    TestPhaseDiscoverServices,
    TestPhaseDiscoverChars,
    TestPhasePreDelay,
    TestPhaseSubscribe,
    TestPhaseWriteKbp,
    TestPhaseWaitResponse,
    TestPhaseDone,
} TestPhase;

typedef struct WhisperPairApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    // GUI modules
    Submenu* submenu;
    View* scan_view;
    Widget* widget;
    TextBox* text_box;
    Popup* popup;
    Loading* loading;

    // Scan state
    WpDevice devices[WP_MAX_DEVICES];
    uint8_t device_count;
    FuriMutex* mutex;
    bool scanning;

    // Timer
    FuriTimer* timer;
    uint32_t tick_count;

    // Selected device
    uint8_t selected_idx;

    // Connection + test state
    bool connected;
    uint16_t connection_handle;
    TestPhase test_phase;
    KbpStrategy current_strategy;
    uint16_t kbp_char_handle;
    uint32_t kbp_write_tick; // when we sent the write
    FuriString* test_log;

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    // KBP response
    uint8_t kbp_response[64];
    uint16_t kbp_response_len;
} WhisperPairApp;
