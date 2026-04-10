// SHA-512 implementation for Ed25519-donna
// Bundled directly since mbedtls SHA-512 isn't exposed in firmware API
#ifndef __SHA2_H__
#define __SHA2_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint64_t state[8];
    uint64_t count[2];
    uint8_t buf[128];
} SHA512_CTX;

void sha512_Init(SHA512_CTX* ctx);
void sha512_Update(SHA512_CTX* ctx, const uint8_t* data, size_t len);
void sha512_Final(SHA512_CTX* ctx, uint8_t* hash);
void sha512_Raw(const uint8_t* data, size_t len, uint8_t* hash);

#endif
