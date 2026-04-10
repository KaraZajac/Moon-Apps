// SHA-512 adapter wrapping mbed TLS for Ed25519-donna
#ifndef __SHA2_H__
#define __SHA2_H__

#include <stdint.h>
#include <stddef.h>
#include <mbedtls/sha512.h>

typedef struct {
    mbedtls_sha512_context ctx;
} SHA512_CTX;

static inline void sha512_Init(SHA512_CTX* c) {
    mbedtls_sha512_init(&c->ctx);
    mbedtls_sha512_starts(&c->ctx, 0); // 0 = SHA-512 (not SHA-384)
}

static inline void sha512_Update(SHA512_CTX* c, const uint8_t* data, size_t len) {
    mbedtls_sha512_update(&c->ctx, data, len);
}

static inline void sha512_Final(SHA512_CTX* c, uint8_t* hash) {
    mbedtls_sha512_finish(&c->ctx, hash);
    mbedtls_sha512_free(&c->ctx);
}

static inline void sha512_Raw(const uint8_t* data, size_t len, uint8_t* hash) {
    mbedtls_sha512(data, len, hash, 0);
}

#endif
