#include "bitchat_identity.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <storage/storage.h>
#include <mbedtls/sha256.h>

#include "../crypto/ed25519_donna/ed25519.h"

#define TAG "BitchatId"

static void derive_peer_id(const uint8_t* noise_public_key, uint8_t* peer_id) {
    // Android: SHA-256(noiseStaticPublicKey), take first 8 bytes
    uint8_t hash[32];
    mbedtls_sha256(noise_public_key, BC_KEY_SIZE, hash, 0);
    memcpy(peer_id, hash, BC_PEER_ID_SIZE);
}

static bool save_identity(const BcIdentity* identity) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data/bitchat"));

    File* file = storage_file_alloc(storage);
    bool success = false;

    if(storage_file_open(file, BC_IDENTITY_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        // Store: ed25519_secret(32) + ed25519_public(32) + noise_secret(32) + noise_public(32)
        uint16_t written = 0;
        written += storage_file_write(file, identity->ed25519_secret, 32);
        written += storage_file_write(file, identity->ed25519_public, 32);
        written += storage_file_write(file, identity->noise_secret, 32);
        written += storage_file_write(file, identity->noise_public, 32);
        success = (written == 128);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

static bool load_identity(BcIdentity* identity) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool success = false;

    if(storage_file_open(file, BC_IDENTITY_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint16_t read = 0;
        read += storage_file_read(file, identity->ed25519_secret, 32);
        read += storage_file_read(file, identity->ed25519_public, 32);
        read += storage_file_read(file, identity->noise_secret, 32);
        read += storage_file_read(file, identity->noise_public, 32);
        success = (read == 128);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

bool bc_identity_load_or_create(BcIdentity* identity) {
    memset(identity, 0, sizeof(BcIdentity));

    if(load_identity(identity)) {
        FURI_LOG_I(TAG, "Identity loaded from SD (128 bytes)");
        derive_peer_id(identity->noise_public, identity->peer_id);
        identity->loaded = true;
        return true;
    }

    // Generate new keypairs
    FURI_LOG_I(TAG, "Generating new Ed25519 + Curve25519 keypairs");

    // Ed25519 signing keypair
    furi_hal_random_fill_buf(identity->ed25519_secret, 32);
    ed25519_publickey(identity->ed25519_secret, identity->ed25519_public);

    // Curve25519 Noise keypair (for key agreement / peer identity)
    furi_hal_random_fill_buf(identity->noise_secret, 32);
    // Clamp the Curve25519 private key per spec
    identity->noise_secret[0] &= 248;
    identity->noise_secret[31] &= 127;
    identity->noise_secret[31] |= 64;
    curve25519_scalarmult_basepoint(identity->noise_public, identity->noise_secret);

    // Derive peer ID from Noise public key
    derive_peer_id(identity->noise_public, identity->peer_id);

    if(save_identity(identity)) {
        FURI_LOG_I(TAG, "Identity saved to SD");
    } else {
        FURI_LOG_W(TAG, "Failed to save identity");
    }

    identity->loaded = true;
    return true;
}

void bc_identity_sign(
    const BcIdentity* identity,
    const uint8_t* data,
    uint16_t data_len,
    uint8_t* sig) {
    ed25519_sign(data, data_len, identity->ed25519_secret, sig);
}
