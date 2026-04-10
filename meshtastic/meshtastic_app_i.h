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
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <furi_ble/gatt_client.h>
#include <gap.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "scenes/meshtastic_scene.h"
#include "helpers/meshtastic_custom_event.h"
#include "helpers/meshtastic_ble.h"
#include "meshtastic/mesh.pb.h"

#define TAG "Meshtastic"

#define MESH_MAX_SCAN_DEVICES  32
#define MESH_MAX_NODES         32
#define MESH_MAX_MESSAGES      32
#define MESH_MAX_CHANNELS      8
#define MESH_TEXT_STORE_SIZE    234
#define MESH_DEVICE_NAME_LEN   32
#define MESH_MSG_TEXT_LEN      234
#define MESH_BLE_READ_BUF_SIZE 512

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[MESH_DEVICE_NAME_LEN];
    bool has_name;
} MeshScanDevice;

typedef struct {
    uint32_t node_num;
    char short_name[5];
    char long_name[40];
    int8_t snr;
    uint32_t last_heard;
    uint8_t battery_level; // 0-100
    bool has_position;
    int32_t latitude;
    int32_t longitude;
} MeshNode;

typedef struct {
    uint32_t from_node;
    uint32_t to_node;
    char text[MESH_MSG_TEXT_LEN];
    uint32_t timestamp;
    uint8_t channel_idx;
} MeshMessage;

typedef struct {
    char name[16];
    uint8_t index;
    uint8_t role; // meshtastic_Channel_Role
} MeshChannel;

typedef enum {
    MeshtasticViewSubmenu,
    MeshtasticViewWidget,
    MeshtasticViewPopup,
    MeshtasticViewLoading,
    MeshtasticViewTextInput,
    MeshtasticViewTextBox,
} MeshtasticViewId;

typedef enum {
    MeshStateIdle,
    MeshStateScanning,
    MeshStateConnecting,
    MeshStateReceivingConfig,
    MeshStateReady,
} MeshConnectionState;

typedef struct MeshtasticApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    Storage* storage;

    // GUI modules
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    TextInput* text_input;
    TextBox* text_box;

    // Text stores
    char text_store[MESH_TEXT_STORE_SIZE];
    FuriString* text_box_store;

    // BLE state
    FuriMutex* mutex;
    FuriTimer* timer;
    MeshConnectionState state;
    uint16_t connection_handle;
    uint32_t poll_count;

    // Meshtastic GATT handles
    MeshtasticCharHandles char_handles;
    uint16_t mesh_service_start_handle;
    uint16_t mesh_service_end_handle;

    // Scan results
    MeshScanDevice scan_devices[MESH_MAX_SCAN_DEVICES];
    uint8_t scan_device_count;
    uint8_t selected_device_idx;

    // My node info
    uint32_t my_node_num;
    char my_long_name[40];
    char my_short_name[5];
    uint32_t config_request_id;
    bool config_complete;

    // Mesh data
    MeshNode nodes[MESH_MAX_NODES];
    uint8_t node_count;
    MeshMessage messages[MESH_MAX_MESSAGES];
    uint8_t message_count;
    MeshChannel channels[MESH_MAX_CHANNELS];
    uint8_t channel_count;

    // Read buffer for FromRadio
    uint8_t read_buf[MESH_BLE_READ_BUF_SIZE];
    uint16_t read_len;
    bool has_read_data;

    // Selected items
    uint8_t selected_node_idx;
    uint8_t selected_channel_idx;
} MeshtasticApp;

MeshtasticApp* meshtastic_app_alloc(void);
void meshtastic_app_free(MeshtasticApp* app);

/** Process a FromRadio protobuf message */
void meshtastic_process_from_radio(MeshtasticApp* app, const uint8_t* data, uint16_t len);

/** Send a ToRadio protobuf message (want_config_id) */
bool meshtastic_send_want_config(MeshtasticApp* app);

/** Send a text message to the mesh */
bool meshtastic_send_text_message(MeshtasticApp* app, const char* text, uint32_t dest, uint8_t channel);

/** Find a node by node number, returns NULL if not found */
MeshNode* meshtastic_find_node(MeshtasticApp* app, uint32_t node_num);

/** Get node name for display (short_name, long_name, or hex ID) */
const char* meshtastic_node_name(MeshtasticApp* app, uint32_t node_num);
