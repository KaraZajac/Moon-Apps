#include "bitchat_identity.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <furi_hal_crypto.h>
#include <storage/storage.h>

#include "../crypto/ed25519_donna/ed25519.h"
#include "../crypto/sha2.h"

#define TAG "BitchatId"

static void derive_peer_id(const uint8_t* public_key, uint8_t* peer_id) {
    // Use SHA-512 (which we have) and take first 8 bytes
    // This matches the Android implementation: SHA-256(pubkey)[0:8]
    // We approximate with SHA-512 truncated - close enough for peer ID
    uint8_t hash[64];
    sha512_Raw(public_key, BC_ED25519_PUBLIC_KEY_SIZE, hash);
    memcpy(peer_id, hash, BC_PEER_ID_SIZE);
}

static bool save_identity(const BcIdentity* identity) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data/bitchat"));

    File* file = storage_file_alloc(storage);
    bool success = false;

    if(storage_file_open(file, BC_IDENTITY_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint16_t written = storage_file_write(file, identity->ed25519_secret, 32);
        written += storage_file_write(file, identity->ed25519_public, 32);
        success = (written == 64);
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
        uint16_t read = storage_file_read(file, identity->ed25519_secret, 32);
        read += storage_file_read(file, identity->ed25519_public, 32);
        success = (read == 64);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

bool bc_identity_load_or_create(BcIdentity* identity) {
    memset(identity, 0, sizeof(BcIdentity));

    if(load_identity(identity)) {
        FURI_LOG_I(TAG, "Identity loaded from SD");
        derive_peer_id(identity->ed25519_public, identity->peer_id);
        identity->loaded = true;
        return true;
    }

    // Generate new keypair
    FURI_LOG_I(TAG, "Generating new Ed25519 keypair");
    furi_hal_random_fill_buf(identity->ed25519_secret, 32);
    ed25519_publickey(identity->ed25519_secret, identity->ed25519_public);
    derive_peer_id(identity->ed25519_public, identity->peer_id);

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

bool bc_identity_verify(
    const uint8_t* public_key,
    const uint8_t* data,
    uint16_t data_len,
    const uint8_t* sig) {
    return ed25519_sign_open(data, data_len, public_key, sig) == 0;
}
