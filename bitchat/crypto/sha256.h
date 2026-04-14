#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buf[64];
} SHA256_CTX;

void sha256_Init(SHA256_CTX* ctx);
void sha256_Update(SHA256_CTX* ctx, const uint8_t* data, size_t len);
void sha256_Final(SHA256_CTX* ctx, uint8_t hash[32]);
void sha256_Raw(const uint8_t* data, size_t len, uint8_t hash[32]);
