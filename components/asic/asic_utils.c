#include "asic_utils.h"


void asic_cpy_hash_reverse_words(const void* const src, void* dst) {
    uint32_t* d = (uint32_t*)dst;
    uint32_t* const end = d + 8;
    const uint32_t* s = ((const uint32_t*)src) + 8 - 1;
    do {
        *d = *s;
        ++d;
        --s;
    } while(d < end);
}