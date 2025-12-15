#include "bm_job_builder.h"
#include "mining.h"
#include "global_state.h"
#include <esp_log.h>

static const char* const TAG = "bm_job_builder";
#define MAX_COINBASE_SIZE  512

static uint8_t coinbase_tx_buf[MAX_COINBASE_SIZE];

static const MemSpan_t COINBASE_BUF_SPAN = {
        .start = coinbase_tx_buf,
        .size = sizeof(coinbase_tx_buf)
    };

bool bm_job_build(mining_notify* const notification,
    uint64_t extranonce_2,
    uint32_t difficulty,
    bool build_midstates,
    bm_job* const out_job)
{
    char *extranonce_2_str = extranonce_2_generate(extranonce_2, GLOBAL_STATE.extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return false;
    }
// printf("xn2: %08" PRIx32 "%08" PRIx32 " -> \"%s\"\n",(uint32_t)(extranonce_2 >> 32), (uint32_t)extranonce_2, extranonce_2_str);
    //print generated extranonce_2
    //ESP_LOGI(TAG, "Generated extranonce_2: %s", extranonce_2_str);
    // out_job->xn2.u64 = extranonce_2;
    // out_job->xn2.size = GLOBAL_STATE.extranonce_2_len;


    Hash_t merkle_root;
    {
        MemSpan_t cbtx = 
            construct_coinbase_tx_bin(
                notification->coinbase_1, notification->coinbase_2, GLOBAL_STATE.extranonce_str, extranonce_2_str,
                COINBASE_BUF_SPAN);

        if(cbtx.size == 0) {
            ESP_LOGE(TAG, "Failed to construct coinbase tx.");
            free(extranonce_2_str);
            return false;
        }
        calculate_merkle_root_hash_bin(cbtx,notification->merkle__branches,&merkle_root);
    }
  
    construct_bm_job(notification, &merkle_root, GLOBAL_STATE.version_mask, difficulty, build_midstates, out_job);

    out_job->extranonce2 = extranonce_2_str; // Transfer ownership
    out_job->jobid = strdup(notification->job_id);
    out_job->version_mask = GLOBAL_STATE.version_mask;

    return true;
}