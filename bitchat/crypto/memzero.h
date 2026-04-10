// Secure memory zeroing
#ifndef __MEMZERO_H__
#define __MEMZERO_H__

#include <string.h>
#include <stddef.h>

static inline void memzero(void* s, size_t n) {
    volatile uint8_t* p = (volatile uint8_t*)s;
    while(n--) *p++ = 0;
}

#endif
