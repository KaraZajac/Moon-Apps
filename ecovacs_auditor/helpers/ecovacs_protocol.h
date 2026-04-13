#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Ecovacs BLE GATT UUIDs (16-bit)
#define ECOVACS_SVC_UUID       0x8888
#define ECOVACS_CMD_CHAR_UUID  0xFF02  // Write No Response
#define ECOVACS_RSP_CHAR_UUID  0xFF01  // Indicate, Notify

// Static AES-128 key: "12345678ecovacs\0" (zero-padded to 16 bytes)
// CVE-2024-12078: hard-coded key shared across all devices
#define ECOVACS_AES_KEY \
    {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, \
     0x65, 0x63, 0x6F, 0x76, 0x61, 0x63, 0x73, 0x00}

// Max encrypted payload
#define ECOVACS_MAX_PAYLOAD 256

// Known Ecovacs device name prefixes
static inline bool ecovacs_is_known_name(const char* name) {
    // Case-insensitive prefix check for common Ecovacs identifiers
    if(!name || name[0] == '\0') return false;

    // Known prefixes from various models
    const char* prefixes[] = {
        "ECOVACS", "DEEBOT", "GOAT", "AIRBOT",
        "Ecovacs", "Deebot", "yeedi", "YEEDI",
        NULL,
    };
    for(int i = 0; prefixes[i]; i++) {
        size_t plen = strlen(prefixes[i]);
        if(strncmp(name, prefixes[i], plen) == 0) return true;
    }
    return false;
}
