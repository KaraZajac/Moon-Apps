#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ── BitChat BLE Constants ────────────────────────────────────────────

// Service UUID: F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C (little-endian)
#define BITCHAT_SVC_UUID_128 \
    {0x5C, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B, \
     0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4}

// Characteristic UUID: A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D (little-endian)
#define BITCHAT_CHAR_UUID_128 \
    {0x5D, 0x4C, 0x3B, 0x2A, 0x1F, 0x0E, 0x9D, 0x8C, \
     0x5B, 0x4A, 0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1}

// ── Packet Types ─────────────────────────────────────────────────────

#define BC_TYPE_ANNOUNCE      0x01
#define BC_TYPE_MESSAGE       0x02
#define BC_TYPE_LEAVE         0x03
#define BC_TYPE_NOISE_HS      0x10
#define BC_TYPE_NOISE_ENC     0x11
#define BC_TYPE_FRAGMENT      0x20

// ── Packet Flags ─────────────────────────────────────────────────────

#define BC_FLAG_HAS_RECIPIENT 0x01
#define BC_FLAG_HAS_SIGNATURE 0x02
#define BC_FLAG_IS_COMPRESSED 0x04
#define BC_FLAG_IS_RSR        0x10

// ── Announce TLV Types ───────────────────────────────────────────────

#define BC_TLV_NICKNAME       0x01
#define BC_TLV_NOISE_PUBKEY   0x02
#define BC_TLV_ED25519_PUBKEY 0x03
#define BC_TLV_NEIGHBOR_IDS   0x04

// ── Protocol Constants ───────────────────────────────────────────────

#define BC_HEADER_SIZE        14
#define BC_SENDER_ID_SIZE     8
#define BC_DEFAULT_TTL        7
#define BC_VERSION            0x01
#define BC_MAX_NICKNAME       20
#define BC_MAX_MSG_CONTENT    200
#define BC_MAX_PAYLOAD        400
#define BC_SIGNATURE_SIZE     64
#define BC_PAD_BLOCK_256      256

// ── Data Structures ──────────────────────────────────────────────────

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t ttl;
    uint64_t timestamp;
    uint8_t flags;
    uint16_t payload_len;
    uint8_t sender_id[BC_SENDER_ID_SIZE];
    uint8_t recipient_id[BC_SENDER_ID_SIZE];
    bool has_recipient;
} BcPacketHeader;

typedef struct {
    char nickname[BC_MAX_NICKNAME + 1];
    uint8_t noise_pubkey[32];
    bool has_noise_key;
    uint8_t ed25519_pubkey[32];
    bool has_ed25519_key;
} BcAnnounce;

typedef struct {
    uint64_t timestamp;
    char sender[BC_MAX_NICKNAME + 1];
    char content[BC_MAX_MSG_CONTENT + 1];
    uint8_t sender_peer_id[BC_SENDER_ID_SIZE];
    bool has_sender_peer_id;
} BcMessage;

// ── Encoding ─────────────────────────────────────────────────────────

// Encode a v1 packet header + sender_id into buf. Returns bytes written (22).
uint16_t bc_encode_header(
    uint8_t* buf,
    uint16_t buf_sz,
    uint8_t type,
    uint8_t ttl,
    uint8_t flags,
    const uint8_t* sender_id,
    const uint8_t* payload,
    uint16_t payload_len);

// Encode an announce packet payload (TLV). Returns bytes written.
uint16_t bc_encode_announce(
    uint8_t* buf,
    uint16_t buf_sz,
    const BcAnnounce* announce);

// Encode a chat message payload. Returns bytes written.
uint16_t bc_encode_message(
    uint8_t* buf,
    uint16_t buf_sz,
    const BcMessage* msg);

// Build a complete announce packet (header + payload). Returns total size.
uint16_t bc_build_announce_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const BcAnnounce* announce);

// Build a signed announce packet. sign_fn is called to produce 64-byte signature.
// Signature is over the packet with ttl=0 and no signature appended.
typedef void (*BcSignFn)(const uint8_t* data, uint16_t len, uint8_t* sig, void* ctx);
uint16_t bc_build_signed_announce_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const BcAnnounce* announce,
    BcSignFn sign_fn,
    void* sign_ctx);

// Build a signed broadcast message packet with raw UTF-8 content.
uint16_t bc_build_signed_broadcast_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const char* content,
    BcSignFn sign_fn,
    void* sign_ctx);

// Build a complete message packet (header + structured payload). Returns total size.
uint16_t bc_build_message_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const BcMessage* msg);

// Apply PKCS#7 padding to buf. Returns new total size (padded).
uint16_t bc_apply_padding(uint8_t* buf, uint16_t data_len, uint16_t buf_sz);

// ── Decoding ─────────────────────────────────────────────────────────

// Decode a packet header from raw bytes. Returns true on success.
bool bc_decode_header(const uint8_t* data, uint16_t len, BcPacketHeader* hdr);

// Decode an announce TLV payload. Returns true on success.
bool bc_decode_announce(
    const uint8_t* payload,
    uint16_t payload_len,
    BcAnnounce* announce);

// Decode a chat message payload. Returns true on success.
bool bc_decode_message(
    const uint8_t* payload,
    uint16_t payload_len,
    BcMessage* msg);
