#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/modules/popup.h>
#include <gui/modules/loading.h>
#include <notification/notification_messages.h>

#include <gap.h>
#include <furi_ble/gatt_client.h>
#include <bt/bt_service/bt.h>

#include "scenes/bitchat_scene.h"
#include "helpers/bitchat_custom_event.h"
#include "helpers/bitchat_protocol.h"
#include "helpers/bitchat_service.h"
#include "helpers/bitchat_profile.h"
#include "helpers/bitchat_identity.h"

#define TAG "BitChat"

#define BC_MAX_PEERS     8
#define BC_MAX_MESSAGES  20

typedef enum {
    BitchatViewSubmenu,
    BitchatViewWidget,
    BitchatViewTextBox,
    BitchatViewTextInput,
    BitchatViewPopup,
    BitchatViewLoading,
} BitchatViewId;

typedef struct {
    uint8_t peer_id[BC_SENDER_ID_SIZE];
    char nickname[BC_MAX_NICKNAME + 1];
    uint8_t ed25519_pubkey[32]; // signing public key (from announce TLV)
    bool has_signing_key;
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint32_t last_seen; // tick
    bool connected;
    // Per-peer central connection (when we connected to them)
    uint16_t central_handle;    // GATT connection handle (central role)
    uint16_t central_char;      // their BitChat characteristic value handle
    bool central_active;        // we have an active central connection to this peer
} BcPeer;

typedef struct {
    char sender[BC_MAX_NICKNAME + 1];
    char content[BC_MAX_MSG_CONTENT + 1];
} BcChatLine;

typedef struct BitchatApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    // GUI
    Submenu* submenu;
    Widget* widget;
    TextBox* text_box;
    TextInput* text_input;
    Popup* popup;
    Loading* loading;

    // Identity (Ed25519 keypair + derived peer ID)
    BcIdentity identity;
    char nickname[BC_MAX_NICKNAME + 1];

    // BLE profile (peripheral side)
    Bt* bt;
    FuriHalBleProfileBase* ble_profile;
    BleServiceBitchat* svc;

    // BLE state
    FuriMutex* mutex;
    bool scanning;
    bool connected;
    uint16_t connection_handle;
    uint16_t bc_char_handle; // BitChat characteristic value handle
    FuriTimer* timer;
    uint32_t tick_count;
    uint8_t our_mac[6]; // Our BLE MAC for tie-breaking

    // Scan results for peer selection
    BcPeer scan_results[BC_MAX_PEERS];
    uint8_t scan_result_count;
    uint8_t selected_scan_idx;

    // Peers
    BcPeer peers[BC_MAX_PEERS];
    uint8_t peer_count;

    // Chat
    BcChatLine messages[BC_MAX_MESSAGES];
    uint8_t message_count;
    FuriString* chat_log;

    // Input buffer
    char input_buf[BC_MAX_MSG_CONTENT + 1];

    // GATT state
    BleGattService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattCharacteristic chars[BLE_GATT_CLIENT_MAX_CHARS];
    uint8_t char_count;

    // Pre-built signed announce packet (sent on peer subscribe)
    uint8_t announce_pkt[256];
    uint16_t announce_pkt_len;

    // Receive buffer
    uint8_t rx_buf[512];
    uint16_t rx_len;

    // Fragment reassembly + deduplication (Phase 3)
    BcFragmentTable frag_table;
    BcDedupTable dedup_table;
} BitchatApp;

// Add a message to the chat log
void bitchat_add_chat_message(BitchatApp* app, const char* sender, const char* content);
