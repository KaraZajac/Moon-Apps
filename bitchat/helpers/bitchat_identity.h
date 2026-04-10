#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BC_IDENTITY_PATH EXT_PATH("apps_data/bitchat/identity.bin")
#define BC_KEY_SIZE 32
#define BC_SIGNATURE_SIZE 64
#define BC_PEER_ID_SIZE 8

typedef struct {
    // Ed25519 signing keypair
    uint8_t ed25519_secret[BC_KEY_SIZE];
    uint8_t ed25519_public[BC_KEY_SIZE];
    // Curve25519 Noise keypair (for announce TLV + peer ID)
    uint8_t noise_secret[BC_KEY_SIZE];
    uint8_t noise_public[BC_KEY_SIZE];
    // Peer ID = SHA-256(noise_public)[0:8]
    uint8_t peer_id[BC_PEER_ID_SIZE];
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
