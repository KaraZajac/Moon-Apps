// Random number adapter using Flipper's hardware RNG
#ifndef __RAND_H__
#define __RAND_H__

#include <furi_hal_random.h>
#include <stdint.h>
#include <stddef.h>

static inline uint32_t random32(void) {
    uint32_t val;
    furi_hal_random_fill_buf((uint8_t*)&val, sizeof(val));
    return val;
}

static inline void random_buffer(uint8_t* buf, size_t len) {
    furi_hal_random_fill_buf(buf, len);
}

#endif
