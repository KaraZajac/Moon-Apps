#include "tracker_detector_sound.h"
#include <string.h>

// BLE GATT characteristic property flags
#define GATT_PROP_WRITE_NO_RESP 0x04
#define GATT_PROP_WRITE         0x08

// Apple AirTag / FindMy accessories
// Service:  7DFC9000-7D1C-4951-86AA-8D9728F8D66C
// Char:     7DFC9001-7D1C-4951-86AA-8D9728F8D66C
// Command:  0x01 0x00 0x03 (play sound)
static const TrackerSoundProfile profile_apple = {
    .tracker_type = TrackerTypeAppleFindMy,
    .service_uuid_type = 2,
    .service_uuid_128 = {0x6C, 0xD6, 0xF8, 0x28, 0x97, 0x8D, 0xAA, 0x86,
                         0x51, 0x49, 0x1C, 0x7D, 0x00, 0x90, 0xFC, 0x7D},
    .char_uuid_type = 2,
    .char_uuid_128 = {0x6C, 0xD6, 0xF8, 0x28, 0x97, 0x8D, 0xAA, 0x86,
                      0x51, 0x49, 0x1C, 0x7D, 0x01, 0x90, 0xFC, 0x7D},
    .command = {0x01, 0x00, 0x03},
    .command_len = 3,
};

// Samsung SmartTag
// Service UUID: 0xFD5A
// Find first writable characteristic, command: 0x01
static const TrackerSoundProfile profile_samsung = {
    .tracker_type = TrackerTypeSamsungSmartTag,
    .service_uuid_type = 1,
    .service_uuid_16 = 0xFD5A,
    .char_uuid_type = 0, // find first writable
    .command = {0x01},
    .command_len = 1,
};

// Tile
// Service UUID: 0xFEED
// Find first writable characteristic, command: 0x05 (ring)
static const TrackerSoundProfile profile_tile = {
    .tracker_type = TrackerTypeTile,
    .service_uuid_type = 1,
    .service_uuid_16 = 0xFEED,
    .char_uuid_type = 0, // find first writable
    .command = {0x05},
    .command_len = 1,
};

// Chipolo
// Service UUID: 0xFE33
// Find first writable characteristic, command: 0x01
static const TrackerSoundProfile profile_chipolo = {
    .tracker_type = TrackerTypeChipolo,
    .service_uuid_type = 1,
    .service_uuid_16 = 0xFE33,
    .char_uuid_type = 0, // find first writable
    .command = {0x01},
    .command_len = 1,
};

// Google Find My Device Network
// Service UUID: 0xFE2C
// Find first writable characteristic, command: 0x01
static const TrackerSoundProfile profile_google = {
    .tracker_type = TrackerTypeGoogleFMDN,
    .service_uuid_type = 1,
    .service_uuid_16 = 0xFE2C,
    .char_uuid_type = 0, // find first writable
    .command = {0x01},
    .command_len = 1,
};

const TrackerSoundProfile* tracker_sound_get_profile(TrackerType type) {
    switch(type) {
    case TrackerTypeAppleFindMy:
        return &profile_apple;
    case TrackerTypeSamsungSmartTag:
        return &profile_samsung;
    case TrackerTypeTile:
        return &profile_tile;
    case TrackerTypeChipolo:
        return &profile_chipolo;
    case TrackerTypeGoogleFMDN:
        return &profile_google;
    default:
        return NULL;
    }
}

bool tracker_sound_match_service(
    const TrackerSoundProfile* profile,
    uint8_t svc_uuid_type,
    uint16_t svc_uuid_16,
    const uint8_t* svc_uuid_128) {
    if(profile->service_uuid_type != svc_uuid_type) return false;
    if(svc_uuid_type == 1) {
        return profile->service_uuid_16 == svc_uuid_16;
    } else {
        return memcmp(profile->service_uuid_128, svc_uuid_128, 16) == 0;
    }
}

bool tracker_sound_match_char(
    const TrackerSoundProfile* profile,
    uint8_t char_uuid_type,
    uint16_t char_uuid_16,
    const uint8_t* char_uuid_128,
    uint8_t properties) {
    if(profile->char_uuid_type == 0) {
        // Match any writable characteristic
        return (properties & (GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RESP)) != 0;
    }
    if(profile->char_uuid_type != char_uuid_type) return false;
    if(char_uuid_type == 1) {
        return profile->char_uuid_16 == char_uuid_16;
    } else {
        return memcmp(profile->char_uuid_128, char_uuid_128, 16) == 0;
    }
}
