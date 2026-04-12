#pragma once

#include <stdint.h>
#include <stdbool.h>

// L2CAP CoC SPSM for file transfer (dynamic range 0x80-0xFF)
#define FT_SPSM          0x00A0
#define FT_MPS           248
#define FT_MTU           4096
#define FT_CREDITS       10
#define FT_MAX_FILENAME  64
#define FT_DATA_CHUNK    247  // 248 MPS - 1 byte for packet type

// Extended advertising service UUID for receiver discovery
// Custom 128-bit UUID: B1EF11E0-0001-4000-8000-00805F9B34FB (little-endian)
#define FT_SVC_UUID_128 \
    {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
     0x00, 0x40, 0x01, 0x00, 0xE0, 0x11, 0xEF, 0xB1}

// Packet types
#define FT_PKT_FILE_HEADER 0x01
#define FT_PKT_FILE_DATA   0x02
#define FT_PKT_FILE_DONE   0x03
#define FT_PKT_FILE_ACK    0x04
#define FT_PKT_FILE_ERROR  0x05

// Extended advertising handle (use 0x01, leave 0x00 for legacy)
#define FT_ADV_HANDLE     0x01
