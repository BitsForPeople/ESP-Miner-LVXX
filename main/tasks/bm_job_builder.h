#pragma once

#include <stdbool.h>
#include "stratum_api.h"
#include "mining.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bm_job_build(
    mining_notify* const notification, 
    uint64_t extranonce_2,
    uint32_t difficulty,
    bool build_midstates,
    bm_job* const out_job);

#ifdef __cplusplus
}
#endif