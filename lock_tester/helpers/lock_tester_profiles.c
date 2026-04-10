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
