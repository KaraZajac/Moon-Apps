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

    /* BitChat's upstream iOS/Android clients expect an *unpaired, unbonded*
     * link — security is done at the application layer via Noise_XX over
     * our custom characteristic. gap_init already configured the auth
     * settings from this profile's GapPairingNone, but that still advertises
     * "just-works" pairing capability (IO_CAP_DISPLAY_YES_NO), which some
     * Android BitChat clients try to take advantage of and then get stuck
     * after the pair completes without ever subscribing to our CCCD.
     * gap_set_no_pairing() broadcasts NoInputNoOutput + LESC unsupported +
     * no bonding + fixed-pin forbidden, so peers skip pairing entirely. */
    gap_set_no_pairing();

    FURI_LOG_I(TAG, "Profile started (unpaired/unbonded GAP auth)");
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
