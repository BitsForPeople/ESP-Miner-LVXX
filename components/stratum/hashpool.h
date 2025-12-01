#pragma once

#include "mining_types.h"
#include "obj_pool_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

bool hashpool_grow_by(const size_t cnt);

uint32_t hashpool_get_size(void);

HashLink_t* hashpool_take(void);

void hashpool_put(HashLink_t* const obj);

uint32_t hashpool_getMaxUse(void);

void hashpool_get_stats(ObjPoolStats_t* const out_stats);

void hashpool_log_stats(void);

#ifdef __cplusplus
}
#endif