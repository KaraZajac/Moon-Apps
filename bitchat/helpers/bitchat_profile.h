#pragma once

#include <furi_ble/profile_interface.h>
#include "bitchat_service.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const FuriHalBleProfileTemplate* ble_profile_bitchat;

void ble_profile_bitchat_set_event_callback(
    FuriHalBleProfileBase* profile,
    uint16_t buff_size,
    BitchatServiceCallback callback,
    void* context);

BleServiceBitchat* ble_profile_bitchat_get_service(FuriHalBleProfileBase* profile);

#ifdef __cplusplus
}
#endif
