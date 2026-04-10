#include "tracker_detector_signatures.h"
#include <string.h>

// BLE AD type constants
#define AD_TYPE_FLAGS             0x01
#define AD_TYPE_INCOMPLETE_16     0x02
#define AD_TYPE_COMPLETE_16       0x03
#define AD_TYPE_INCOMPLETE_128    0x06
#define AD_TYPE_COMPLETE_128      0x07
#define AD_TYPE_SHORT_NAME        0x08
#define AD_TYPE_COMPLETE_NAME     0x09
#define AD_TYPE_SERVICE_DATA_16   0x16
#define AD_TYPE_SERVICE_DATA_128  0x21
#define AD_TYPE_MANUFACTURER_DATA 0xFF

// Company IDs (little-endian in AD data)
#define COMPANY_APPLE_LO   0x4C
#define COMPANY_APPLE_HI   0x00
#define COMPANY_SAMSUNG_LO 0x75
#define COMPANY_SAMSUNG_HI 0x00

// Apple FindMy payload type
#define APPLE_FINDMY_TYPE   0x12
#define APPLE_FINDMY_LENGTH 0x19

// Service UUIDs (little-endian)
#define UUID_TILE_FEED_LO    0xED
#define UUID_TILE_FEED_HI    0xFE
#define UUID_TILE_FEEC_LO    0xEC
#define UUID_TILE_FEEC_HI    0xFE
#define UUID_CHIPOLO_LO      0x33
#define UUID_CHIPOLO_HI      0xFE
#define UUID_GOOGLE_FMDN_LO  0x2C
#define UUID_GOOGLE_FMDN_HI  0xFE

const char* tracker_type_get_name(TrackerType type) {
    switch(type) {
    case TrackerTypeAppleFindMy:
        return "Apple FindMy";
    case TrackerTypeSamsungSmartTag:
        return "Samsung SmartTag";
    case TrackerTypeTile:
        return "Tile";
    case TrackerTypeChipolo:
        return "Chipolo";
    case TrackerTypeGoogleFMDN:
        return "Google FMDN";
    default:
        return "Unknown";
    }
}

// Check if a 16-bit service UUID (little-endian) matches known tracker UUIDs
static TrackerType check_service_uuid_16(uint8_t lo, uint8_t hi) {
    if((lo == UUID_TILE_FEED_LO && hi == UUID_TILE_FEED_HI) ||
       (lo == UUID_TILE_FEEC_LO && hi == UUID_TILE_FEEC_HI)) {
        return TrackerTypeTile;
    }
    if(lo == UUID_CHIPOLO_LO && hi == UUID_CHIPOLO_HI) {
        return TrackerTypeChipolo;
    }
    if(lo == UUID_GOOGLE_FMDN_LO && hi == UUID_GOOGLE_FMDN_HI) {
        return TrackerTypeGoogleFMDN;
    }
    return TrackerTypeUnknown;
}

// Walk 16-bit UUID list looking for tracker matches
static TrackerType scan_uuid_list_16(const uint8_t* data, uint8_t len) {
    for(uint8_t i = 0; i + 1 < len; i += 2) {
        TrackerType t = check_service_uuid_16(data[i], data[i + 1]);
        if(t != TrackerTypeUnknown) return t;
    }
    return TrackerTypeUnknown;
}

// Check service data (16-bit UUID prefix) for tracker matches
static TrackerType check_service_data_16(const uint8_t* data, uint8_t len) {
    if(len < 2) return TrackerTypeUnknown;
    return check_service_uuid_16(data[0], data[1]);
}

TrackerSignatureResult tracker_signature_identify(const uint8_t* data, uint8_t data_len) {
    TrackerSignatureResult result = {
        .type = TrackerTypeUnknown,
        .detected = false,
    };

    if(!data || data_len == 0) return result;

    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t field_len = data[pos];
        if(field_len == 0 || pos + field_len >= data_len) break;

        uint8_t ad_type = data[pos + 1];
        const uint8_t* field_data = &data[pos + 2];
        uint8_t field_data_len = field_len - 1;

        switch(ad_type) {
        case AD_TYPE_MANUFACTURER_DATA:
            if(field_data_len >= 2) {
                uint8_t company_lo = field_data[0];
                uint8_t company_hi = field_data[1];

                // Apple FindMy: company 0x004C, type byte 0x12, length 0x19
                if(company_lo == COMPANY_APPLE_LO && company_hi == COMPANY_APPLE_HI) {
                    if(field_data_len >= 4 && field_data[2] == APPLE_FINDMY_TYPE &&
                       field_data[3] == APPLE_FINDMY_LENGTH) {
                        result.type = TrackerTypeAppleFindMy;
                        result.detected = true;
                        return result;
                    }
                }

                // Samsung SmartTag: company 0x0075
                if(company_lo == COMPANY_SAMSUNG_LO && company_hi == COMPANY_SAMSUNG_HI) {
                    result.type = TrackerTypeSamsungSmartTag;
                    result.detected = true;
                    return result;
                }
            }
            break;

        case AD_TYPE_INCOMPLETE_16:
        case AD_TYPE_COMPLETE_16: {
            TrackerType t = scan_uuid_list_16(field_data, field_data_len);
            if(t != TrackerTypeUnknown) {
                result.type = t;
                result.detected = true;
                return result;
            }
            break;
        }

        case AD_TYPE_SERVICE_DATA_16: {
            TrackerType t = check_service_data_16(field_data, field_data_len);
            if(t != TrackerTypeUnknown) {
                result.type = t;
                result.detected = true;
                return result;
            }
            break;
        }

        default:
            break;
        }

        pos += field_len + 1;
    }

    return result;
}
