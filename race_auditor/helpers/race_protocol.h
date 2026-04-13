#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// RACE packet header
#define RACE_HEAD_STANDARD  0x05
#define RACE_HEAD_FOTA      0x15

// RACE message types
#define RACE_TYPE_CMD       0x5A  // Command expecting response
#define RACE_TYPE_RESP      0x5B  // Response
#define RACE_TYPE_CMD_NORESP 0x5C // Fire-and-forget
#define RACE_TYPE_INDICATION 0x5D // Unsolicited indication

// RACE command IDs
#define RACE_CMD_SDK_VERSION      0x0301
#define RACE_CMD_FLASH_WRITE      0x0402
#define RACE_CMD_FLASH_READ       0x0403
#define RACE_CMD_PARTITION_ERASE  0x0404
#define RACE_CMD_GET_LINK_KEYS    0x0CC0
#define RACE_CMD_GET_BD_ADDR      0x0CD5
#define RACE_CMD_READ_RAM         0x1680
#define RACE_CMD_BUILD_VERSION    0x1E08

// Flash base address for vulnerability check
#define RACE_FLASH_BASE           0x08000000

// Max response buffer
#define RACE_MAX_RESPONSE         512
#define RACE_HEADER_SIZE          6

// GATT Service UUIDs (little-endian byte order)
// Airoha default: 5052494D-2DAB-0341-6972-6F6861424C45
#define RACE_SVC_UUID_AIROHA \
    {0x45, 0x4C, 0x42, 0x61, 0x68, 0x6F, 0x72, 0x69, \
     0x41, 0x03, 0xAB, 0x2D, 0x4D, 0x49, 0x52, 0x50}

// Sony variant: dc405470-a351-4a59-97d8-2e2e3b207fbb
#define RACE_SVC_UUID_SONY \
    {0xBB, 0x7F, 0x20, 0x3B, 0x2E, 0x2E, 0xD8, 0x97, \
     0x59, 0x4A, 0x51, 0xA3, 0x70, 0x54, 0x40, 0xDC}

// GATT Characteristic UUIDs (little-endian)
// Airoha TX (write): 43484152-2DAB-3241-6972-6F6861424C45
#define RACE_TX_UUID_AIROHA \
    {0x45, 0x4C, 0x42, 0x61, 0x68, 0x6F, 0x72, 0x69, \
     0x41, 0x32, 0xAB, 0x2D, 0x52, 0x41, 0x48, 0x43}

// Airoha RX (notify): 43484152-2DAB-3141-6972-6F6861424C45
#define RACE_RX_UUID_AIROHA \
    {0x45, 0x4C, 0x42, 0x61, 0x68, 0x6F, 0x72, 0x69, \
     0x41, 0x31, 0xAB, 0x2D, 0x52, 0x41, 0x48, 0x43}

// Sony TX (write): bfd869fa-a3f2-4c2f-bcff-3eb1ec80cead
#define RACE_TX_UUID_SONY \
    {0xAD, 0xCE, 0x80, 0xEC, 0xB1, 0x3E, 0xFF, 0xBC, \
     0x2F, 0x4C, 0xF2, 0xA3, 0xFA, 0x69, 0xD8, 0xBF}

// Sony RX (notify): 2a6b6575-faf6-418c-923f-ccd63a56d955
#define RACE_RX_UUID_SONY \
    {0x55, 0xD9, 0x56, 0x3A, 0xD6, 0xCC, 0x3F, 0x92, \
     0x8C, 0x41, 0xF6, 0xFA, 0x75, 0x65, 0x6B, 0x2A}

typedef enum {
    RaceVariantNone,
    RaceVariantAiroha,
    RaceVariantSony,
} RaceVariant;

// Build a RACE command packet. Returns total packet length.
// buf must be at least RACE_HEADER_SIZE + payload_len bytes.
static inline uint16_t race_build_cmd(
    uint8_t* buf,
    uint16_t cmd_id,
    const uint8_t* payload,
    uint16_t payload_len) {
    buf[0] = RACE_HEAD_STANDARD;
    buf[1] = RACE_TYPE_CMD;
    uint16_t length = payload_len + 2; // includes 2-byte cmd ID
    buf[2] = length & 0xFF;
    buf[3] = (length >> 8) & 0xFF;
    buf[4] = cmd_id & 0xFF;
    buf[5] = (cmd_id >> 8) & 0xFF;
    if(payload && payload_len > 0) {
        memcpy(&buf[6], payload, payload_len);
    }
    return RACE_HEADER_SIZE + payload_len;
}

// Parse response command ID from a RACE packet
static inline uint16_t race_parse_cmd_id(const uint8_t* data, uint16_t len) {
    if(len < RACE_HEADER_SIZE) return 0;
    return data[4] | (data[5] << 8);
}

// Parse response payload (skip header). Returns pointer to payload, sets payload_len.
static inline const uint8_t* race_parse_payload(
    const uint8_t* data,
    uint16_t len,
    uint16_t* payload_len) {
    if(len <= RACE_HEADER_SIZE) {
        *payload_len = 0;
        return NULL;
    }
    *payload_len = len - RACE_HEADER_SIZE;
    return &data[RACE_HEADER_SIZE];
}

// Check if a 128-bit UUID matches a known RACE service UUID.
// Returns RaceVariant.
static inline RaceVariant race_match_service_uuid(const uint8_t* uuid_128) {
    static const uint8_t airoha_svc[] = RACE_SVC_UUID_AIROHA;
    static const uint8_t sony_svc[] = RACE_SVC_UUID_SONY;
    if(memcmp(uuid_128, airoha_svc, 16) == 0) return RaceVariantAiroha;
    if(memcmp(uuid_128, sony_svc, 16) == 0) return RaceVariantSony;
    return RaceVariantNone;
}

// Check if a 128-bit UUID matches a RACE TX characteristic
static inline bool race_is_tx_char(const uint8_t* uuid_128, RaceVariant variant) {
    static const uint8_t airoha_tx[] = RACE_TX_UUID_AIROHA;
    static const uint8_t sony_tx[] = RACE_TX_UUID_SONY;
    if(variant == RaceVariantAiroha) return memcmp(uuid_128, airoha_tx, 16) == 0;
    if(variant == RaceVariantSony) return memcmp(uuid_128, sony_tx, 16) == 0;
    return false;
}

// Check if a 128-bit UUID matches a RACE RX characteristic
static inline bool race_is_rx_char(const uint8_t* uuid_128, RaceVariant variant) {
    static const uint8_t airoha_rx[] = RACE_RX_UUID_AIROHA;
    static const uint8_t sony_rx[] = RACE_RX_UUID_SONY;
    if(variant == RaceVariantAiroha) return memcmp(uuid_128, airoha_rx, 16) == 0;
    if(variant == RaceVariantSony) return memcmp(uuid_128, sony_rx, 16) == 0;
    return false;
}
