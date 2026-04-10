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
#include <gui/elements.h>
#include <notification/notification_messages.h>

#include <gap.h>
#include <furi_ble/gatt_client.h>

#include "scenes/tracker_detector_scene.h"
#include "helpers/tracker_detector_custom_event.h"
#include "helpers/tracker_detector_signatures.h"
#include "helpers/tracker_detector_tracker.h"
#include "helpers/tracker_detector_sound.h"

#define TAG "TrackerDetector"

typedef enum {
    TrackerDetectorViewSubmenu,
    TrackerDetectorViewScanList,
    TrackerDetectorViewWidget,
    TrackerDetectorViewPopup,
    TrackerDetectorViewLoading,
} TrackerDetectorViewId;

typedef enum {
    PlaySoundStateConnecting,
    PlaySoundStateDiscoveringServices,
    PlaySoundStateDiscoveringChars,
    PlaySoundStateWritingCommand,
    PlaySoundStateDone,
    PlaySoundStateFailed,
} PlaySoundState;

// Model for the custom scan list view
typedef struct {
    TrackerDevice trackers[TRACKER_MAX_DEVICES];
    uint8_t tracker_count;
    int16_t cursor;
    int16_t scroll;
    bool scanning;
    uint32_t scan_start_tick;
} TrackerDetectorScanModel;

typedef struct TrackerDetectorApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    // GUI modules
    Submenu* submenu;
    View* scan_view;
    Widget* widget;
    Popup* popup;
    Loading* loading;

    // Scan state (protected by mutex, written from GAP callback thread)
    TrackerDevice trackers[TRACKER_MAX_DEVICES];
    uint8_t tracker_count;
    FuriMutex* mutex;
    bool scanning;

    // Periodic timer for UI refresh and scan restart
    FuriTimer* tick_timer;
    uint32_t scan_start_tick;

    // Selected tracker for detail view
    uint8_t selected_tracker_idx;

    // Connection state for play sound
    bool connected;
    uint16_t connection_handle;
    uint32_t connect_poll_count;
    PlaySoundState play_sound_state;
    char status_text[64];

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;
} TrackerDetectorApp;
