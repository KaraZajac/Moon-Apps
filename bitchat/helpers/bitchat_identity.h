#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BC_IDENTITY_PATH EXT_PATH("apps_data/bitchat/identity.bin")
#define BC_ED25519_SECRET_KEY_SIZE 32
#define BC_ED25519_PUBLIC_KEY_SIZE 32
#define BC_ED25519_SIGNATURE_SIZE 64
#define BC_PEER_ID_SIZE 8

typedef struct {
    uint8_t ed25519_secret[BC_ED25519_SECRET_KEY_SIZE];
    uint8_t ed25519_public[BC_ED25519_PUBLIC_KEY_SIZE];
    uint8_t peer_id[BC_PEER_ID_SIZE]; // SHA-256(ed25519_public)[0:8]
    bool loaded;
} BcIdentity;

// Load identity from SD card, or generate new if not found
bool bc_identity_load_or_create(BcIdentity* identity);

// Sign data with Ed25519. sig must be 64 bytes.
void bc_identity_sign(
    const BcIdentity* identity,
    const uint8_t* data,
    uint16_t data_len,
    uint8_t* sig);

// Verify Ed25519 signature. Returns true if valid.
bool bc_identity_verify(
    const uint8_t* public_key,
    const uint8_t* data,
    uint16_t data_len,
    const uint8_t* sig);
