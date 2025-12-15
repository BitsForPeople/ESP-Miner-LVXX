#include <lwip/tcpip.h>

#include "global_state.h"
#include "system.h"

#include "serial.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"
#include "stratum_task.h"
#include "asic.h"

static const char* const TAG = "asic_result";

#ifndef LIKELY
    #define LIKELY(x)       __builtin_expect(!!(x),1)
#endif
#ifndef UNLIKELY
    #define UNLIKELY(x)     __builtin_expect(!!(x),0)
#endif

void ASIC_result_task(void *pvParameters)
{
    // GlobalState* const GLOBAL_STATE = (GlobalState *)pvParameters;

    while (1)
    {
        asm volatile ("":"+m" (GLOBAL_STATE));
        //task_result *asic_result = (*GLOBAL_STATE.ASIC_functions.receive_result_fn)(GLOBAL_STATE);
        task_result* const asic_result = ASIC_process_work(&GLOBAL_STATE);

        if (UNLIKELY(asic_result == NULL))
        {
            continue;
        }

        unsigned job_id = asic_result->job_id;

        if (UNLIKELY(GLOBAL_STATE.valid_jobs[job_id] == 0))
        {
            ESP_LOGI(TAG, "Job no longer valid, 0x%02X", job_id);
            continue;
        }

        bm_job* const active_job = GLOBAL_STATE.ASIC_TASK_MODULE.active_jobs[job_id];

        // check the nonce difficulty
        const uint64_t nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        //log the ASIC response
        // ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);
        // ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %" PRIu64 " of %" PRIu32 ".", active_job->jobid, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);
        ESP_LOGI(TAG, "Result diff %" PRIu64 " of %" PRIu32 ".", nonce_diff, active_job->pool_diff);

        if (nonce_diff >= active_job->pool_diff || nonce_diff >= GLOBAL_STATE.pool_difficulty)
        {
            char* const user = GLOBAL_STATE.SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE.SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE.SYSTEM_MODULE.pool_user;
            int ret = STRATUM_V1_submit_share(
                GLOBAL_STATE.sock,
                // GLOBAL_STATE.send_uid++,
                STRATUM_V1_next_submit_id(),
                user,
                active_job->jobid,
                active_job->extranonce2,
                active_job->ntime,
                asic_result->nonce,
                asic_result->rolled_version ^ active_job->version);

            if (UNLIKELY(ret < 0)) {
                ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
                stratum_close_connection();
            }
        }

        SYSTEM_notify_found_nonce(nonce_diff, job_id);
    }
}
