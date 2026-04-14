#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/widget.h>
#include <gui/modules/loading.h>
#include <gui/modules/dialog_ex.h>
#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include <gap.h>
#include <furi_ble/gatt_client.h>
#include <furi_ble/l2cap_coc.h>

#include "scenes/ft_scene.h"
#include "helpers/ft_custom_event.h"
#include "helpers/file_transfer_protocol.h"

#define TAG "BleFileTransfer"

#define FT_MAX_SCAN_DEVICES 16

typedef enum {
    FtViewSubmenu,
    FtViewWidget,
    FtViewPopup,
    FtViewLoading,
    FtViewDialogEx,
} FtViewId;

typedef enum {
    FtModeIdle,
    FtModeSending,
    FtModeReceiving,
} FtMode;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[32];
    bool has_name;
} FtScanDevice;

typedef struct FtApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    Storage* storage;
    DialogsApp* dialogs;

    // GUI
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    DialogEx* dialog_ex;

    // BLE state (protected by mutex)
    FuriMutex* mutex;
    FuriTimer* timer;
    uint32_t tick_count;

    // Dual-role: always advertising as receiver
    bool adv_active;

    // Send state (central role)
    bool send_connected;
    uint16_t send_handle;
    uint8_t send_coc_channel;
    bool send_coc_connected;
    uint16_t tx_credits;
    bool scanning;

    // Receive state (peripheral role — incoming connections)
    bool recv_connected;
    uint16_t recv_handle;
    uint8_t recv_coc_channel;
    bool recv_coc_connected;

    // Scan results
    FtScanDevice scan_devices[FT_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;

    // Transfer state
    FtMode mode;
    FuriString* file_path;
    char filename[FT_MAX_FILENAME];
    uint32_t file_size;
    uint32_t bytes_transferred;
    bool transfer_complete;
    bool transfer_error;

    // File handles
    File* rx_file;
    File* tx_file;

    // Target device for sending
    uint8_t target_addr[6];
    uint8_t target_addr_type;
} FtApp;
