#pragma once

#include <stdint.h>

/** Look up a human-readable name for a 16-bit BLE UUID.
 *  Returns the name string or NULL if unknown. */
const char* ble_connect_uuid_name(uint16_t uuid);
