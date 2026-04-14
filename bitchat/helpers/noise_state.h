#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Noise_XX_25519_ChaChaPoly_SHA256
// Matches Android (noise-java) and iOS (CryptoKit) BitChat implementations

#define NOISE_KEY_SIZE  32
#define NOISE_TAG_SIZE  16
#define NOISE_HASH_SIZE 32

// Handshake message sizes
#define NOISE_MSG1_SIZE  32  // -> e
#define NOISE_MSG2_SIZE  96  // <- e, ee, s, es
#define NOISE_MSG3_SIZE  48  // -> s, se

typedef struct {
    uint8_t k[NOISE_KEY_SIZE]; // cipher key (zeroed if not set)
    uint32_t n;                // nonce counter
    bool has_key;
} NoiseCipherState;

typedef struct {
    uint8_t ck[NOISE_HASH_SIZE]; // chaining key
    uint8_t h[NOISE_HASH_SIZE];  // handshake hash
    NoiseCipherState cs;
} NoiseSymmetricState;

typedef enum {
    NoiseRoleInitiator,
    NoiseRoleResponder,
} NoiseRole;

typedef struct {
    NoiseSymmetricState ss;
    NoiseRole role;
    uint8_t s_priv[NOISE_KEY_SIZE];  // our static private key
    uint8_t s_pub[NOISE_KEY_SIZE];   // our static public key
    uint8_t e_priv[NOISE_KEY_SIZE];  // our ephemeral private key
    uint8_t e_pub[NOISE_KEY_SIZE];   // our ephemeral public key
    uint8_t rs[NOISE_KEY_SIZE];      // remote static public key
    uint8_t re[NOISE_KEY_SIZE];      // remote ephemeral public key
    uint8_t msg_idx;                 // 0, 1, or 2 (which message we're on)
    bool complete;
} NoiseHandshakeState;

// Session: holds transport keys after handshake completes
typedef struct {
    NoiseCipherState send_cipher;
    NoiseCipherState recv_cipher;
    uint8_t remote_static[NOISE_KEY_SIZE]; // peer's static key
    bool active;
    uint32_t created_tick;
} NoiseSession;

// Initialize a handshake as initiator or responder.
// s_priv/s_pub: our static Curve25519 keypair
void noise_handshake_init(
    NoiseHandshakeState* hs,
    NoiseRole role,
    const uint8_t s_priv[32],
    const uint8_t s_pub[32]);

// Write the next handshake message.
// Returns bytes written to out_buf, or 0 on error.
uint16_t noise_handshake_write(
    NoiseHandshakeState* hs,
    uint8_t* out_buf,
    uint16_t out_buf_sz);

// Read a handshake message from the peer.
// Returns true on success. After msg3 is processed, hs->complete is true.
bool noise_handshake_read(
    NoiseHandshakeState* hs,
    const uint8_t* msg,
    uint16_t msg_len);

// After handshake completes, extract transport session.
// Zeroes the handshake state.
bool noise_handshake_split(
    NoiseHandshakeState* hs,
    NoiseSession* session);

// Encrypt a message with the session's send cipher.
// plaintext in buf, ciphertext+tag written. Returns total output size.
// Output format: [4B nonce BE][ciphertext][16B tag]
uint16_t noise_session_encrypt(
    NoiseSession* session,
    const uint8_t* plaintext, uint16_t pt_len,
    uint8_t* out_buf, uint16_t out_buf_sz);

// Decrypt a message with the session's recv cipher.
// Input format: [4B nonce BE][ciphertext][16B tag]
// Returns plaintext length, or 0 on failure.
uint16_t noise_session_decrypt(
    NoiseSession* session,
    const uint8_t* encrypted, uint16_t enc_len,
    uint8_t* out_buf, uint16_t out_buf_sz);
