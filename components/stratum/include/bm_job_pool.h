#pragma once

#include "mining.h"
#include "obj_pool_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bmjobpool_grow_by(const size_t cnt);

uint32_t bmjobpool_get_size(void);

bm_job* bmjobpool_take(void);

void bmjobpool_put(bm_job* const obj);

uint32_t bmjobpool_getMaxUse(void);

void bmjobpool_get_stats(ObjPoolStats_t* const out_stats);

void bmjobpool_log_stats(void);

#ifdef __cplusplus
}
#endif