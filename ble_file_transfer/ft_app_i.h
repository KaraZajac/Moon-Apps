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

#define FT_RX_BUF_SIZE 4096

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

    // BLE state
    FuriMutex* mutex;
    FuriTimer* timer;
    uint32_t tick_count;
    bool connected;
    uint16_t connection_handle;
    uint8_t coc_channel_index;
    bool coc_connected;
    bool scanning;

    // Transfer state
    FtMode mode;
    FuriString* file_path;
    char filename[FT_MAX_FILENAME];
    uint32_t file_size;
    uint32_t bytes_transferred;
    bool transfer_complete;
    bool transfer_error;

    // Receive buffer
    File* rx_file;

    // Send state
    File* tx_file;
    uint16_t tx_credits;

    // Target device for sending
    uint8_t target_addr[6];
    uint8_t target_addr_type;
} FtApp;
