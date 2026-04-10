#include "ble_connect_device.h"
#include <furi.h>
#include <string.h>
#include <stdio.h>

#define TAG "BleConnectDevice"

bool ble_connect_device_parse_adv_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t len = data[pos];
        if(len == 0 || pos + len >= data_len) break;
        uint8_t type = data[pos + 1];
        // 0x08 = Shortened Local Name, 0x09 = Complete Local Name
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

void ble_connect_device_mac_to_str(const uint8_t* address, char* str, size_t str_size) {
    snprintf(
        str,
        str_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address[5],
        address[4],
        address[3],
        address[2],
        address[1],
        address[0]);
}

bool ble_connect_device_save(
    Storage* storage,
    const char* file_path,
    const BleConnectDevice* device,
    const BleGattService* services,
    uint8_t service_count) {
    furi_check(storage);
    furi_check(file_path);
    furi_check(device);

    // Ensure directory exists
    storage_simply_mkdir(storage, BLE_CONNECT_APP_FOLDER);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool success = false;

    do {
        if(!flipper_format_file_open_always(ff, file_path)) break;
        if(!flipper_format_write_header_cstr(ff, "BLE Connect Device", 1)) break;
        if(!flipper_format_write_string_cstr(ff, "Name", device->name)) break;
        if(!flipper_format_write_hex(ff, "Address", device->address, 6)) break;
        uint32_t addr_type = device->address_type;
        if(!flipper_format_write_uint32(ff, "AddressType", &addr_type, 1)) break;
        uint32_t svc_count = service_count;
        if(!flipper_format_write_uint32(ff, "ServiceCount", &svc_count, 1)) break;
        for(uint8_t i = 0; i < service_count; i++) {
            if(!flipper_format_write_hex(
                   ff, "Service", (const uint8_t*)&services[i], sizeof(BleGattService)))
                break;
        }
        success = true;
    } while(false);

    flipper_format_free(ff);

    if(success) {
        FURI_LOG_I(TAG, "Saved device '%s' to %s", device->name, file_path);
    } else {
        FURI_LOG_E(TAG, "Failed to save device to %s", file_path);
    }
    return success;
}

bool ble_connect_device_load(
    Storage* storage,
    const char* file_path,
    BleConnectDevice* device) {
    furi_check(storage);
    furi_check(file_path);
    furi_check(device);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool success = false;
    FuriString* temp_str = furi_string_alloc();

    do {
        if(!flipper_format_file_open_existing(ff, file_path)) break;

        uint32_t version = 0;
        if(!flipper_format_read_header(ff, temp_str, &version)) break;
        if(furi_string_cmp_str(temp_str, "BLE Connect Device") != 0 || version != 1) break;

        if(!flipper_format_read_string(ff, "Name", temp_str)) break;
        strlcpy(device->name, furi_string_get_cstr(temp_str), BLE_CONNECT_DEVICE_NAME_LEN);
        device->has_name = true;

        if(!flipper_format_read_hex(ff, "Address", device->address, 6)) break;

        uint32_t addr_type = 0;
        if(!flipper_format_read_uint32(ff, "AddressType", &addr_type, 1)) break;
        device->address_type = (uint8_t)addr_type;

        device->rssi = 0; // Not stored
        success = true;
    } while(false);

    furi_string_free(temp_str);
    flipper_format_free(ff);

    if(success) {
        FURI_LOG_I(TAG, "Loaded device '%s' from %s", device->name, file_path);
    } else {
        FURI_LOG_E(TAG, "Failed to load device from %s", file_path);
    }
    return success;
}
