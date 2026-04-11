#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BITCHAT_DATA_LEN_MAX 247

typedef enum {
    BitchatServiceEventDataReceived,
    BitchatServiceEventDataSent,
    BitchatServiceEventPeerSubscribed, // A peer subscribed to our notifications
} BitchatServiceEventType;

typedef struct {
    BitchatServiceEventType event;
    struct {
        const uint8_t* buffer;
        uint16_t size;
    } data;
} BitchatServiceEvent;

// Return value: bytes available in buffer
typedef uint16_t (*BitchatServiceCallback)(BitchatServiceEvent event, void* context);

typedef struct BleServiceBitchat BleServiceBitchat;

BleServiceBitchat* ble_svc_bitchat_start(void);
void ble_svc_bitchat_stop(BleServiceBitchat* svc);

void ble_svc_bitchat_set_callbacks(
    BleServiceBitchat* svc,
    uint16_t buff_size,
    BitchatServiceCallback callback,
    void* context);

bool ble_svc_bitchat_tx(BleServiceBitchat* svc, const uint8_t* data, uint16_t data_len);

#ifdef __cplusplus
}
#endif
