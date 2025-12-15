#pragma once
#include "mining.h"
#include "stratum_api.h"

#ifdef __cplusplus
extern "C" {
#endif

void bldwrk( const mining_notify* mn,
    const char* xn1,
    uint32_t xn2len);

void setXn2(const char* xn2);
void cmpcb(const MemSpan_t cb);
#ifdef __cplusplus
}
#endif