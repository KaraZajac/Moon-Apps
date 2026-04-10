#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "tracker_detector_signatures.h"

#define TRACKER_MAX_DEVICES     64
#define TRACKER_NAME_LEN        28
#define TRACKER_AD_DATA_MAX     31
#define TRACKER_FOLLOW_TIMEOUT_S 600  // 10 minutes: flag as "following"
#define TRACKER_STALE_TIMEOUT_S  120  // 2 minutes without seeing: mark stale

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    TrackerType type;
    int8_t rssi;
    int8_t rssi_min;
    int8_t rssi_max;
    char name[TRACKER_NAME_LEN];
    bool has_name;
    uint32_t first_seen;   // furi_get_tick() timestamp
    uint32_t last_seen;
    uint32_t hit_count;    // number of scan hits
    bool following;        // flagged as potential follower
    bool alerted;          // already alerted user for this device
    uint8_t ad_data[TRACKER_AD_DATA_MAX];
    uint8_t ad_data_len;
} TrackerDevice;
