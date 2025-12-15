#pragma once
#include "mining.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    // ASIC may not return the nonce in the same order as the jobs were sent
    // it also may return a previous nonce under some circumstances
    // so we keep a list of jobs indexed by the job id
    bm_job **active_jobs;
} AsicTaskModule;

void ASIC_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

