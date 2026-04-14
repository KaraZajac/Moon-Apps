#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ChaCha20-Poly1305 AEAD (IETF construction, RFC 8439)
// 32-byte key, 12-byte nonce, 16-byte tag

#define CHACHA20POLY1305_KEY_SIZE  32
#define CHACHA20POLY1305_NONCE_SIZE 12
#define CHACHA20POLY1305_TAG_SIZE  16

// Encrypt in-place: plaintext in buf, ciphertext + tag written to buf.
// buf must have room for len + 16 bytes.
void chacha20poly1305_encrypt(
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t* ad, size_t ad_len,
    uint8_t* buf, size_t len,
    uint8_t tag[16]);

// Decrypt in-place: ciphertext in buf, plaintext written to buf.
// Returns true if tag is valid.
bool chacha20poly1305_decrypt(
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t* ad, size_t ad_len,
    uint8_t* buf, size_t len,
    const uint8_t tag[16]);
