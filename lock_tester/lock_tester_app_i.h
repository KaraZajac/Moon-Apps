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
#include <gui/modules/loading.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/text_box.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#include <gap.h>
#include <furi_ble/gatt_client.h>

#include "scenes/lock_tester_scene.h"
#include "helpers/lock_tester_custom_event.h"
#include "helpers/lock_tester_profiles.h"

#define TAG "LockTester"

#define LOCK_TESTER_MAX_SCAN_DEVICES 48
#define LOCK_TESTER_DEVICE_NAME_LEN  32
#define LOCK_TESTER_BYTE_STORE_SIZE  32

typedef enum {
    LockTesterViewSubmenu,
    LockTesterViewWidget,
    LockTesterViewPopup,
    LockTesterViewLoading,
    LockTesterViewByteInput,
    LockTesterViewTextBox,
} LockTesterViewId;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[LOCK_TESTER_DEVICE_NAME_LEN];
    bool has_name;
    const LockProfile* profile;  // matched lock profile, or NULL
} LockTesterDevice;

typedef struct LockTesterApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    // GUI modules
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    ByteInput* byte_input;
    TextBox* text_box;

    // Scan state
    LockTesterDevice scan_devices[LOCK_TESTER_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    FuriMutex* mutex;
    bool scanning;

    // Connection state
    bool connected;
    uint16_t connection_handle;
    uint32_t connect_poll_count;
    FuriTimer* timer;

    // Current device
    LockTesterDevice current_device;
    uint8_t selected_device_idx;

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    // Test state
    const LockProfile* active_profile;
    uint8_t current_test_idx;
    uint16_t target_char_handle;  // characteristic to write tests to
    bool test_running;
    FuriString* test_log;

    // Manual write
    uint8_t byte_store[LOCK_TESTER_BYTE_STORE_SIZE];
    uint8_t byte_store_len;
    uint16_t write_char_handle;

    // Notification data from lock
    uint8_t notify_buf[64];
    uint16_t notify_len;
    bool has_notification;

    // Selected service index for char discovery
    uint8_t selected_service_idx;
} LockTesterApp;
