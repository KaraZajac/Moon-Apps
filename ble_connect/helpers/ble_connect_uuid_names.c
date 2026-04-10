#include "ble_connect_uuid_names.h"
#include <stddef.h>

typedef struct {
    uint16_t uuid;
    const char* name;
} BleUuidEntry;

static const BleUuidEntry known_uuids[] = {
    // GATT Services
    {0x1800, "Generic Access"},
    {0x1801, "Generic Attribute"},
    {0x1802, "Immediate Alert"},
    {0x1803, "Link Loss"},
    {0x1804, "Tx Power"},
    {0x1805, "Current Time"},
    {0x180A, "Device Information"},
    {0x180D, "Heart Rate"},
    {0x180F, "Battery Service"},
    {0x1810, "Blood Pressure"},
    {0x1812, "HID"},
    {0x1816, "Cycling Speed/Cadence"},
    {0x181A, "Environmental Sensing"},
    {0x181C, "User Data"},
    {0x1822, "Pulse Oximeter"},
    {0x6BA1, "Meshtastic"},
    // GATT Characteristics
    {0x2A00, "Device Name"},
    {0x2A01, "Appearance"},
    {0x2A02, "Periph Privacy Flag"},
    {0x2A04, "Periph Conn Params"},
    {0x2A05, "Service Changed"},
    {0x2A19, "Battery Level"},
    {0x2A24, "Model Number"},
    {0x2A25, "Serial Number"},
    {0x2A26, "Firmware Rev"},
    {0x2A27, "Hardware Rev"},
    {0x2A28, "Software Rev"},
    {0x2A29, "Manufacturer Name"},
    {0x2A37, "Heart Rate Measurement"},
    {0x2A38, "Body Sensor Location"},
    {0x2A6E, "Temperature"},
    {0x2A6F, "Humidity"},
    {0x2A6D, "Pressure"},
    {0x2902, "CCCD"},
};

const char* ble_connect_uuid_name(uint16_t uuid) {
    for(size_t i = 0; i < sizeof(known_uuids) / sizeof(known_uuids[0]); i++) {
        if(known_uuids[i].uuid == uuid) return known_uuids[i].name;
    }
    return NULL;
}
