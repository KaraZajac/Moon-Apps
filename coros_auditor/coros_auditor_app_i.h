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

#include "scenes/coros_scene.h"
#include "helpers/coros_custom_event.h"
#include "helpers/coros_protocol.h"

#define TAG "CorosAuditor"

#define COROS_MAX_SCAN_DEVICES 48
#define COROS_DEVICE_NAME_LEN  32

typedef enum {
    CorosViewSubmenu,
    CorosViewWidget,
    CorosViewPopup,
    CorosViewLoading,
    CorosViewTextBox,
} CorosViewId;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[COROS_DEVICE_NAME_LEN];
    bool has_name;
    bool is_coros;
} CorosDevice;

typedef enum {
    CorosAuditReadInfo,     // Read standard GATT (battery, serial, model, SW)
    CorosAuditFindDevice,   // Send beep command (proves cmd execution)
    CorosAuditFakeNotif,    // Send fake notification (proves CVE-2025-32879)
    CorosAuditDone,
} CorosAuditPhase;

typedef struct CorosAuditorApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    TextBox* text_box;

    CorosDevice scan_devices[COROS_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    FuriMutex* mutex;
    bool scanning;

    bool connected;
    uint16_t connection_handle;
    uint32_t connect_poll_count;
    FuriTimer* timer;

    CorosDevice current_device;

    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    // COROS-specific handles
    uint16_t cmd_write_handle;    // Main command channel write
    uint16_t notif_write_handle;  // Notification channel write
    bool has_cmd_svc;
    bool has_notif_svc;

    // Standard service handles
    uint16_t battery_handle;
    uint16_t model_handle;
    uint16_t serial_handle;
    uint16_t sw_rev_handle;

    // Audit state
    CorosAuditPhase audit_phase;
    uint8_t read_step; // sub-step within ReadInfo phase
    FuriString* audit_log;

    // Response buffer
    uint8_t read_buf[256];
    uint16_t read_len;
    bool has_read;
} CorosAuditorApp;

void coros_scan_callback(GapScanResultData* result, void* context);
bool coros_parse_adv_name(const uint8_t* data, uint8_t data_len, char* name, size_t name_size);
