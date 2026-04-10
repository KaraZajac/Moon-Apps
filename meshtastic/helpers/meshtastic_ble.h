#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Meshtastic BLE GATT UUIDs */

/* Service UUID: 6ba1b218-15a8-461f-9fa8-5dcae273eafd */
static const uint8_t MESH_SERVICE_UUID[16] = {
    0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f,
    0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b};

/* ToRadio UUID: f75c76d2-129e-4dad-a1dd-7866124401e7 */
static const uint8_t MESH_TORADIO_UUID[16] = {
    0xe7, 0x01, 0x44, 0x12, 0x66, 0x78, 0xdd, 0xa1,
    0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7};

/* FromRadio UUID: 2c55e69e-4993-11ed-b878-0242ac120002 */
static const uint8_t MESH_FROMRADIO_UUID[16] = {
    0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8,
    0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c};

/* FromNum UUID: ed9da18c-a800-4f66-a670-aa7547e34453 */
static const uint8_t MESH_FROMNUM_UUID[16] = {
    0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6,
    0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed};

/** Check if a 128-bit UUID matches the Meshtastic service */
bool meshtastic_is_mesh_service(const uint8_t* uuid128);

/** Find Meshtastic characteristic handles within discovered characteristics */
typedef struct {
    uint16_t toradio_handle;
    uint16_t fromradio_handle;
    uint16_t fromnum_handle;
    bool all_found;
} MeshtasticCharHandles;
