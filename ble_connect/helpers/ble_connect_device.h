#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi_ble/gatt_client.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define BLE_CONNECT_MAX_SCAN_DEVICES 48
#define BLE_CONNECT_DEVICE_NAME_LEN  32
#define BLE_CONNECT_APP_FOLDER       EXT_PATH("apps_data/ble_connect")
#define BLE_CONNECT_APP_EXTENSION    ".ble"

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[BLE_CONNECT_DEVICE_NAME_LEN];
    bool has_name;
} BleConnectDevice;

/** Parse device name from BLE advertising data */
bool ble_connect_device_parse_adv_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size);

/** Format MAC address as string "AA:BB:CC:DD:EE:FF" */
void ble_connect_device_mac_to_str(const uint8_t* address, char* str, size_t str_size);

/** Save device profile to SD card */
bool ble_connect_device_save(
    Storage* storage,
    const char* file_path,
    const BleConnectDevice* device,
    const BleGattService* services,
    uint8_t service_count);

/** Load device profile from SD card */
bool ble_connect_device_load(
    Storage* storage,
    const char* file_path,
    BleConnectDevice* device);
