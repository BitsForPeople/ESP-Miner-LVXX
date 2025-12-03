#pragma once

#include <stdbool.h>
#include "global_state.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bm_job_build(GlobalState* const GLOBAL_STATE, mining_notify* const notification, uint64_t extranonce_2, uint32_t difficulty, bm_job* const out_job);

#ifdef __cplusplus
}
#endif