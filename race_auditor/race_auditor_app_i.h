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

#include "scenes/race_scene.h"
#include "helpers/race_custom_event.h"
#include "helpers/race_protocol.h"

#define TAG "RaceAuditor"

#define RACE_MAX_SCAN_DEVICES 48
#define RACE_DEVICE_NAME_LEN  32

typedef enum {
    RaceViewSubmenu,
    RaceViewWidget,
    RaceViewPopup,
    RaceViewLoading,
    RaceViewTextBox,
} RaceViewId;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[RACE_DEVICE_NAME_LEN];
    bool has_name;
} RaceDevice;

typedef enum {
    AuditPhaseSubscribe,
    AuditPhaseSdkVersion,
    AuditPhaseBuildVersion,
    AuditPhaseBdAddress,
    AuditPhaseFlashCheck,
    AuditPhaseLinkKeys,
    AuditPhaseDone,
} AuditPhase;

typedef struct RaceAuditorApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    // GUI
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    TextBox* text_box;

    // Scan state
    RaceDevice scan_devices[RACE_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    FuriMutex* mutex;
    bool scanning;

    // Connection state
    bool connected;
    uint16_t connection_handle;
    uint32_t connect_poll_count;
    FuriTimer* timer;

    // Current device
    RaceDevice current_device;
    uint8_t selected_device_idx;

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    // RACE state
    RaceVariant variant;
    uint16_t tx_handle;  // RACE TX characteristic value handle
    uint16_t rx_handle;  // RACE RX characteristic value handle
    uint8_t race_service_idx; // index into services[]

    // Audit state
    AuditPhase audit_phase;
    FuriString* audit_log;
    bool vulnerable;

    // Response buffer
    uint8_t resp_buf[RACE_MAX_RESPONSE];
    uint16_t resp_len;
    bool has_response;
} RaceAuditorApp;

// Scan callback (defined in app.c, used by scan scene)
void race_scan_callback(GapScanResultData* result, void* context);

// Parse device name from advertisement
bool race_parse_adv_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size);
