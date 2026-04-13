#pragma once

#include <stdint.h>

// Fuzz test categories
typedef enum {
    FuzzTestWriteAll,       // Write random data to every characteristic
    FuzzTestOversizeWrite,  // Write progressively larger payloads
    FuzzTestInvalidHandle,  // Write/read to non-existent handles
    FuzzTestRapidSubscribe, // Rapid subscribe/unsubscribe cycles
    FuzzTestMtuFuzz,        // Request extreme MTU values
    FuzzTestReadAll,        // Read every handle including invalid ones
    FuzzTestNum,
} FuzzTestId;

typedef struct {
    const char* name;
    const char* desc;
} FuzzTestInfo;

static const FuzzTestInfo fuzz_test_info[FuzzTestNum] = {
    [FuzzTestWriteAll]       = {"Write All Chars",  "Write random data to every char"},
    [FuzzTestOversizeWrite]  = {"Oversize Writes",  "Writes exceeding MTU/limits"},
    [FuzzTestInvalidHandle]  = {"Invalid Handles",  "Read/write non-existent handles"},
    [FuzzTestRapidSubscribe] = {"Rapid Subscribe",  "Fast subscribe/unsub cycles"},
    [FuzzTestMtuFuzz]        = {"MTU Fuzz",         "Extreme MTU exchange values"},
    [FuzzTestReadAll]        = {"Read All Handles",  "Read handles 0x0001-0xFFFF"},
};
