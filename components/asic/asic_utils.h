#pragma once
#include <stdint.h>

#ifdef __cplusplus 
extern "C" {
#endif

void asic_cpy_hash_reverse_words(const void* const src, void* dst);

#ifdef __cplusplus 
}
#endif