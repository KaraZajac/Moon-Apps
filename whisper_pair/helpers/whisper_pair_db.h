#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ── Fast Pair protocol constants ─────────────────────────────────────

#define FP_SVC_UUID          0xFE2C
#define FP_AD_TYPE_SVC_DATA  0x16

// KBP characteristic: FE2C1234-8366-4814-8EB0-01DE32100BEA
// In little-endian uuid_128, bytes 12-13 are 0x34, 0x12
#define FP_KBP_UUID_BYTE12   0x34
#define FP_KBP_UUID_BYTE13   0x12

// ── KBP test strategies ──────────────────────────────────────────────

typedef enum {
    KbpStrategyRawKbp,           // 0x00, 0x11, addr[6], salt[8]
    KbpStrategyRawWithSeeker,    // 0x00, 0x02, addr[6], seeker[6], salt[2]
    KbpStrategyRetroactive,      // 0x00, 0x0A, addr[6], seeker[6], salt[2]
    KbpStrategyExtendedResponse, // 0x00, 0x10, addr[6], salt[8]
    KbpStrategyCount,
} KbpStrategy;

static inline const char* kbp_strategy_name(KbpStrategy s) {
    switch(s) {
    case KbpStrategyRawKbp: return "RAW_KBP";
    case KbpStrategyRawWithSeeker: return "WITH_SEEKER";
    case KbpStrategyRetroactive: return "RETROACTIVE";
    case KbpStrategyExtendedResponse: return "EXTENDED";
    default: return "?";
    }
}

// ── Known device database ────────────────────────────────────────────

typedef struct {
    uint32_t model_id;
    const char* name;
    bool vulnerable;
    uint16_t pre_write_delay_ms; // device-specific delay before KBP write
} WpKnownDevice;

static const WpKnownDevice wp_known_devices[] = {
    // Google
    {0x30018E, "Pixel Buds Pro 2", true, 500},
    // Sony
    {0xCD8256, "Sony WF-1000XM4", true, 1000},
    {0x0E30C3, "Sony WH-1000XM5", true, 1000},
    {0xD5BC6B, "Sony WH-1000XM6", true, 1000},
    {0x821F66, "Sony LinkBuds S", true, 1000},
    // JBL
    {0xF52494, "JBL Tune Buds", true, 200},
    {0x718FA4, "JBL Live Pro 2", true, 200},
    {0xD446A7, "JBL Tune Beam", true, 200},
    // Anker/Soundcore
    {0x9D3F8A, "Soundcore Lib 4", true, 0},
    {0xF0B77F, "Soundcore L4 NC", true, 0},
    // Nothing
    {0xD0A72C, "Nothing Ear (a)", true, 0},
    // OnePlus
    {0xD97EBA, "OnePlus Buds 3P", true, 0},
    // Bose
    {0xF00002, "Bose QC Earbuds II", true, 0},
    // Beats
    {0x000006, "Beats Studio Buds+", true, 0},
    // Xiaomi
    {0xAE3989, "Redmi Buds 5 Pro", true, 0},
    // Jabra
    {0xD446F9, "Jabra Elite 8", true, 0},
    // Samsung (patched)
    {0x0082DA, "Galaxy Buds2 Pro", false, 0},
    {0x00FA72, "Galaxy Buds FE", false, 0},
};
#define WP_KNOWN_DEVICE_COUNT (sizeof(wp_known_devices) / sizeof(wp_known_devices[0]))

static inline const WpKnownDevice* wp_db_lookup(uint32_t model_id) {
    for(size_t i = 0; i < WP_KNOWN_DEVICE_COUNT; i++) {
        if(wp_known_devices[i].model_id == model_id) return &wp_known_devices[i];
    }
    return NULL;
}

// ── Fast Pair advertisement parsing ──────────────────────────────────

typedef struct {
    bool is_fast_pair;
    uint32_t model_id;
    bool in_pairing_mode; // true = pairing mode (3-byte svc data), false = idle
} WpAdvParseResult;

static inline WpAdvParseResult wp_parse_fast_pair_adv(const uint8_t* data, uint8_t len) {
    WpAdvParseResult result = {false, 0, false};
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t ad_len = data[pos];
        if(ad_len == 0 || pos + ad_len >= len) break;
        uint8_t ad_type = data[pos + 1];
        if(ad_type == FP_AD_TYPE_SVC_DATA && ad_len >= 5) {
            if(data[pos + 2] == (FP_SVC_UUID & 0xFF) &&
               data[pos + 3] == (FP_SVC_UUID >> 8)) {
                uint8_t svc_data_len = ad_len - 3; // minus type + UUID
                if(svc_data_len >= 3) {
                    result.is_fast_pair = true;
                    result.model_id = ((uint32_t)data[pos + 4] << 16) |
                                      ((uint32_t)data[pos + 5] << 8) |
                                      (uint32_t)data[pos + 6];
                    // 3 bytes = pairing mode, >3 = idle with account key filter
                    result.in_pairing_mode = (svc_data_len == 3);
                    return result;
                }
            }
        }
        pos += ad_len + 1;
    }
    return result;
}

static inline bool wp_parse_name(const uint8_t* data, uint8_t len, char* name, size_t sz) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t l = data[pos];
        if(l == 0 || pos + l >= len) break;
        if(data[pos + 1] == 0x08 || data[pos + 1] == 0x09) {
            uint8_t nl = l - 1;
            if(nl >= sz) nl = sz - 1;
            for(uint8_t i = 0; i < nl; i++) name[i] = data[pos + 2 + i];
            name[nl] = '\0';
            return true;
        }
        pos += l + 1;
    }
    return false;
}
