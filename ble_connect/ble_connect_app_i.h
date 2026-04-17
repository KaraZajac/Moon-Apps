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
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/text_box.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/dialog_ex.h>
#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include <furi_ble/gatt_client.h>
#include <gap.h>

#include "scenes/ble_connect_scene.h"
#include "helpers/ble_connect_custom_event.h"
#include "helpers/ble_connect_device.h"
#include "helpers/ble_connect_uuid_names.h"

#define TAG "BleConnect"

#define BLE_CONNECT_TEXT_STORE_SIZE 128
#define BLE_CONNECT_BYTE_STORE_SIZE 64
#define BLE_CONNECT_CONNECT_TIMEOUT_MS 10000

typedef enum {
    BleConnectViewSubmenu,
    BleConnectViewWidget,
    BleConnectViewPopup,
    BleConnectViewLoading,
    BleConnectViewTextInput,
    BleConnectViewByteInput,
    BleConnectViewTextBox,
    BleConnectViewDialogEx,
    BleConnectViewVariableItemList,
} BleConnectViewId;

typedef struct BleConnectApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    Storage* storage;
    DialogsApp* dialogs;

    // GUI modules
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    TextInput* text_input;
    ByteInput* byte_input;
    TextBox* text_box;
    DialogEx* dialog_ex;
    VariableItemList* variable_item_list;

    // Text/data stores
    char text_store[BLE_CONNECT_TEXT_STORE_SIZE];
    FuriString* text_box_store;
    uint8_t byte_store[BLE_CONNECT_BYTE_STORE_SIZE];
    uint8_t byte_store_len;

    // Scanner state
    BleConnectDevice scan_devices[BLE_CONNECT_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    FuriMutex* scan_mutex;
    bool scanning;

    // Connection state
    bool connected;
    uint16_t connection_handle;
    FuriTimer* connect_timer;
    uint32_t pairing_pin; // For PIN display/entry
    uint32_t connect_poll_count; // Track timeout during connection polling

    // Current device
    BleConnectDevice current_device;
    uint8_t selected_device_idx;

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;
    uint8_t selected_service_idx;
    uint8_t selected_char_idx;

    // Read data
    uint8_t read_buf[256];
    uint16_t read_len;
    uint16_t read_handle;
    bool has_read_data;

    // File path for save/load
    FuriString* file_path;
} BleConnectApp;

BleConnectApp* ble_connect_app_alloc(void);
void ble_connect_app_free(BleConnectApp* app);

/** Register the GATT client callback for this app's active connection.
 *  Call after app->connection_handle has been populated. */
void ble_connect_register_gatt_callback(BleConnectApp* app);
