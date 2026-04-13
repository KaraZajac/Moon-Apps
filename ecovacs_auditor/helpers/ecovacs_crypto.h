#pragma once

#include <stdint.h>

// AES-128-ECB encrypt a single 16-byte block in-place
void aes128_ecb_encrypt(const uint8_t key[16], uint8_t data[16]);

// AES-128-ECB decrypt a single 16-byte block in-place
void aes128_ecb_decrypt(const uint8_t key[16], uint8_t data[16]);

// Encrypt a buffer with AES-128-ECB (PKCS7 padding).
// out must be at least ((in_len / 16) + 1) * 16 bytes.
// Returns the output length (always a multiple of 16).
uint16_t aes128_ecb_encrypt_padded(
    const uint8_t key[16],
    const uint8_t* in,
    uint16_t in_len,
    uint8_t* out);

// Decrypt a buffer with AES-128-ECB (PKCS7 unpadding).
// in_len must be a multiple of 16.
// Returns the unpadded output length, or 0 on error.
uint16_t aes128_ecb_decrypt_unpadded(
    const uint8_t key[16],
    const uint8_t* in,
    uint16_t in_len,
    uint8_t* out);
