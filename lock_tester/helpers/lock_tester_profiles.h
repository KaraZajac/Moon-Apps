#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define LOCK_TEST_CMD_MAX    16
#define LOCK_PROFILE_TESTS_MAX 12
#define LOCK_PROFILE_COUNT   5

typedef struct {
    const char* name;
    const uint8_t* data;
    uint8_t data_len;
} LockTestCommand;

typedef struct {
    const char* name;
    const char* name_prefix;    // Match device name prefix (NULL = skip)
    uint16_t service_uuid;      // Match advertisement/GATT service UUID (0 = skip)
    uint16_t target_char_uuid;  // Specific characteristic to write to (0 = first writable)
    const LockTestCommand* tests;
    uint8_t test_count;
    const char* description;
} LockProfile;

// Get all profiles
const LockProfile* lock_profiles_get_all(uint8_t* count);

// Match a device against profiles by name
const LockProfile* lock_profile_match_by_name(const char* device_name);

// Match by GATT service UUID (after connection + discovery)
const LockProfile* lock_profile_match_by_service(uint16_t uuid_16);

// Parse device name from raw AD data
bool lock_tester_parse_adv_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size);

// Check if AD data contains a known lock service UUID
uint16_t lock_tester_check_adv_services(const uint8_t* data, uint8_t data_len);
