#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "tracker_detector_signatures.h"

#define SOUND_CMD_MAX_LEN 8

typedef struct {
    TrackerType tracker_type;
    uint8_t service_uuid_type; // 1=16-bit, 2=128-bit
    uint16_t service_uuid_16;
    uint8_t service_uuid_128[16];
    uint8_t char_uuid_type; // 0=find first writable, 1=16-bit, 2=128-bit
    uint16_t char_uuid_16;
    uint8_t char_uuid_128[16];
    uint8_t command[SOUND_CMD_MAX_LEN];
    uint8_t command_len;
} TrackerSoundProfile;

// Get the sound profile for a tracker type, or NULL if unsupported
const TrackerSoundProfile* tracker_sound_get_profile(TrackerType type);

// Check if a discovered service matches a sound profile
bool tracker_sound_match_service(
    const TrackerSoundProfile* profile,
    uint8_t svc_uuid_type,
    uint16_t svc_uuid_16,
    const uint8_t* svc_uuid_128);

// Check if a discovered characteristic matches a sound profile
// If profile->char_uuid_type == 0, matches any characteristic with write property
bool tracker_sound_match_char(
    const TrackerSoundProfile* profile,
    uint8_t char_uuid_type,
    uint16_t char_uuid_16,
    const uint8_t* char_uuid_128,
    uint8_t properties);
