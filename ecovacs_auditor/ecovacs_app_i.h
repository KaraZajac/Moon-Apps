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

#include "scenes/ecovacs_scene.h"
#include "helpers/ecovacs_custom_event.h"
#include "helpers/ecovacs_protocol.h"
#include "helpers/ecovacs_crypto.h"

#define TAG "EcovacsAuditor"

#define ECOVACS_MAX_SCAN_DEVICES 32
#define ECOVACS_DEVICE_NAME_LEN  32

typedef enum {
    EcovacsViewSubmenu,
    EcovacsViewWidget,
    EcovacsViewPopup,
    EcovacsViewLoading,
    EcovacsViewTextBox,
} EcovacsViewId;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[ECOVACS_DEVICE_NAME_LEN];
    bool has_name;
    bool is_ecovacs; // matched by name prefix
} EcovacsDevice;

typedef enum {
    EcoAuditSubscribe,
    EcoAuditProbeUnencrypted,
    EcoAuditProbeEncrypted,
    EcoAuditDone,
} EcoAuditPhase;

typedef struct EcovacsAuditorApp {
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
    EcovacsDevice scan_devices[ECOVACS_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    FuriMutex* mutex;
    bool scanning;

    // Connection state
    bool connected;
    uint16_t connection_handle;
    uint32_t connect_poll_count;
    FuriTimer* timer;

    // Current device
    EcovacsDevice current_device;
    uint8_t selected_device_idx;

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    // Ecovacs service state
    bool has_ecovacs_svc;
    uint16_t cmd_handle;  // 0xFF02 value handle
    uint16_t rsp_handle;  // 0xFF01 value handle

    // Audit state
    EcoAuditPhase audit_phase;
    FuriString* audit_log;
    bool svc_vulnerable;   // service 0x8888 exposed without auth
    bool key_vulnerable;   // static AES key works

    // Response buffer
    uint8_t resp_buf[ECOVACS_MAX_PAYLOAD];
    uint16_t resp_len;
    bool has_response;
} EcovacsAuditorApp;

void ecovacs_scan_callback(GapScanResultData* result, void* context);

bool ecovacs_parse_adv_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size);
