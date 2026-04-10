#include "bitchat_service.h"
#include "bitchat_protocol.h"

#include <furi.h>
#include <furi_ble/gatt.h>
#include <furi_ble/event_dispatcher.h>

#include "../headers/app_common.h"
#include "../headers/ble_vs_codes.h"
#include "../headers/ble_gatt_aci.h"

#define TAG "BitchatSvc"

static const Service_UUID_t bitchat_svc_uuid = {.Service_UUID_128 = BITCHAT_SVC_UUID_128};

typedef struct {
    const void* data_ptr;
    uint16_t data_len;
} BitchatDataWrapper;

static bool bitchat_data_callback(const void* context, const uint8_t** data, uint16_t* data_len) {
    const BitchatDataWrapper* wrapper = context;
    if(data) {
        *data = wrapper->data_ptr;
        *data_len = wrapper->data_len;
    } else {
        *data_len = BITCHAT_DATA_LEN_MAX;
    }
    return false;
}

static BleGattCharacteristicParams bitchat_chars[] = {
    [0] = {
        .name = "BitChat",
        .data_prop_type = FlipperGattCharacteristicDataCallback,
        .data.callback.fn = bitchat_data_callback,
        .data.callback.context = NULL,
        .uuid.Char_UUID_128 = BITCHAT_CHAR_UUID_128,
        .uuid_type = UUID_TYPE_128,
        .char_properties = CHAR_PROP_WRITE_WITHOUT_RESP | CHAR_PROP_WRITE | CHAR_PROP_NOTIFY,
        .security_permissions = ATTR_PERMISSION_NONE,
        .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
        .is_variable = CHAR_VALUE_LEN_VARIABLE,
    },
};

struct BleServiceBitchat {
    uint16_t svc_handle;
    BleGattCharacteristicInstance chars[1];
    BitchatServiceCallback callback;
    void* context;
    GapSvcEventHandler* event_handler;
};

static BleEventAckStatus bitchat_event_handler(void* event, void* context) {
    BleServiceBitchat* svc = (BleServiceBitchat*)context;
    BleEventAckStatus ret = BleEventNotAck;

    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);
    evt_blecore_aci* blecore_evt = (evt_blecore_aci*)event_pckt->data;

    if(event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return ret;

    if(blecore_evt->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
        aci_gatt_attribute_modified_event_rp0* attr_mod =
            (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;

        // CCCD write (subscribe/unsubscribe to notifications)
        if(attr_mod->Attr_Handle == svc->chars[0].handle + 2) {
            FURI_LOG_D(TAG, "CCCD write: %d bytes", attr_mod->Attr_Data_Length);
            ret = BleEventAckFlowEnable;
        }
        // Characteristic value write (incoming data from peer)
        else if(attr_mod->Attr_Handle == svc->chars[0].handle + 1) {
            FURI_LOG_I(TAG, "RX %d bytes from peer", attr_mod->Attr_Data_Length);

            if(svc->callback) {
                BitchatServiceEvent evt = {
                    .event = BitchatServiceEventDataReceived,
                    .data = {
                        .buffer = attr_mod->Attr_Data,
                        .size = attr_mod->Attr_Data_Length,
                    }};
                svc->callback(evt, svc->context);
            }
            ret = BleEventAckFlowEnable;
        }
    } else if(blecore_evt->ecode == ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE) {
        if(svc->callback) {
            BitchatServiceEvent evt = {.event = BitchatServiceEventDataSent};
            svc->callback(evt, svc->context);
        }
        ret = BleEventAckFlowEnable;
    }

    return ret;
}

BleServiceBitchat* ble_svc_bitchat_start(void) {
    BleServiceBitchat* svc = malloc(sizeof(BleServiceBitchat));
    memset(svc, 0, sizeof(BleServiceBitchat));

    svc->event_handler = ble_event_dispatcher_register_svc_handler(
        bitchat_event_handler, svc);

    if(!ble_gatt_service_add(
           UUID_TYPE_128,
           &bitchat_svc_uuid,
           PRIMARY_SERVICE,
           12,
           &svc->svc_handle)) {
        FURI_LOG_E(TAG, "Failed to add GATT service");
        free(svc);
        return NULL;
    }

    ble_gatt_characteristic_init(
        svc->svc_handle, &bitchat_chars[0], &svc->chars[0]);

    FURI_LOG_I(TAG, "Service started, handle=0x%04X, char handle=0x%04X",
        svc->svc_handle, svc->chars[0].handle);

    return svc;
}

void ble_svc_bitchat_stop(BleServiceBitchat* svc) {
    furi_check(svc);
    ble_event_dispatcher_unregister_svc_handler(svc->event_handler);
    ble_gatt_characteristic_delete(svc->svc_handle, &svc->chars[0]);
    ble_gatt_service_delete(svc->svc_handle);
    free(svc);
}

void ble_svc_bitchat_set_callbacks(
    BleServiceBitchat* svc,
    uint16_t buff_size,
    BitchatServiceCallback callback,
    void* context) {
    furi_check(svc);
    (void)buff_size;
    svc->callback = callback;
    svc->context = context;
}

bool ble_svc_bitchat_tx(BleServiceBitchat* svc, const uint8_t* data, uint16_t data_len) {
    if(data_len > BITCHAT_DATA_LEN_MAX) return false;

    BitchatDataWrapper wrapper = {.data_ptr = data, .data_len = data_len};
    return ble_gatt_characteristic_update(svc->svc_handle, &svc->chars[0], &wrapper);
}
