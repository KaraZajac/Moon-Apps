#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TrackerTypeUnknown = 0,
    TrackerTypeAppleFindMy,
    TrackerTypeSamsungSmartTag,
    TrackerTypeTile,
    TrackerTypeChipolo,
    TrackerTypeGoogleFMDN,
    TrackerTypeCount,
} TrackerType;

typedef struct {
    TrackerType type;
    int8_t rssi;
    bool detected;
} TrackerSignatureResult;

const char* tracker_type_get_name(TrackerType type);

// Parse raw AD data and identify tracker type
TrackerSignatureResult tracker_signature_identify(const uint8_t* data, uint8_t data_len);
