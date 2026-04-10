#include "bitchat_profile.h"
#include "bitchat_protocol.h"

#include <furi.h>
#include <furi_hal.h>
#include <gap.h>
#include <ble/core/auto/ble_types.h>

#define TAG "BitchatProfile"

typedef struct {
    FuriHalBleProfileBase base;
    BleServiceBitchat* svc;
} BleProfileBitchat;

static const GapConfig bitchat_gap_config = {
    .adv_service = {
        .UUID_Type = UUID_TYPE_128,
        .Service_UUID_128 = BITCHAT_SVC_UUID_128,
    },
    .mfg_data_len = 0,
    .appearance_char = 0,
    .bonding_mode = false,
    .pairing_method = GapPairingNone,
    .adv_name = "BC",
    .conn_param = {
        .conn_int_min = 0x18, // 30ms
        .conn_int_max = 0x24, // 45ms
        .slave_latency = 0,
        .supervisor_timeout = 0,
    },
};

static void bitchat_profile_get_config(GapConfig* config, FuriHalBleProfileParams params) {
    (void)params;
    memcpy(config, &bitchat_gap_config, sizeof(GapConfig));
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
}

static FuriHalBleProfileBase* bitchat_profile_start(FuriHalBleProfileParams params) {
    (void)params;
    BleProfileBitchat* profile = malloc(sizeof(BleProfileBitchat));
    profile->base.config = ble_profile_bitchat;
    profile->svc = ble_svc_bitchat_start();
    furi_check(profile->svc);
    FURI_LOG_I(TAG, "Profile started");
    return &profile->base;
}

static void bitchat_profile_stop(FuriHalBleProfileBase* profile) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_bitchat);
    BleProfileBitchat* bp = (BleProfileBitchat*)profile;
    ble_svc_bitchat_stop(bp->svc);
    free(bp);
    FURI_LOG_I(TAG, "Profile stopped");
}

static const FuriHalBleProfileTemplate bitchat_profile_template = {
    .start = bitchat_profile_start,
    .stop = bitchat_profile_stop,
    .get_gap_config = bitchat_profile_get_config,
};

const FuriHalBleProfileTemplate* ble_profile_bitchat = &bitchat_profile_template;

void ble_profile_bitchat_set_event_callback(
    FuriHalBleProfileBase* profile,
    uint16_t buff_size,
    BitchatServiceCallback callback,
    void* context) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_bitchat);
    BleProfileBitchat* bp = (BleProfileBitchat*)profile;
    ble_svc_bitchat_set_callbacks(bp->svc, buff_size, callback, context);
}

BleServiceBitchat* ble_profile_bitchat_get_service(FuriHalBleProfileBase* profile) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_bitchat);
    return ((BleProfileBitchat*)profile)->svc;
}
