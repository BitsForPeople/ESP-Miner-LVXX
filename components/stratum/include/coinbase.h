#pragma once

#include "mining_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COINBASE_MAX_SIZE (256+64)


struct CB_mem {
    uint16_t sz;
    uint16_t xn2Pos;
    uint8_t xn2Len;
    uint8_t u8[COINBASE_MAX_SIZE];
};

typedef struct CB_mem* CB_handle_t; 

void coinbase_init(CB_handle_t cb);
size_t coinbase_append(CB_handle_t cb, MemSpan_t mem);
size_t coinbase_append_xn2(CB_handle_t cb, size_t len);
void coinbase_set_xn2(CB_handle_t cb, uint64_t xn2);
MemSpan_t coinbase_get(CB_handle_t cb);
MemSpan_t coinbase_get_space(CB_handle_t cb);

#ifdef __cplusplus
}
#endif